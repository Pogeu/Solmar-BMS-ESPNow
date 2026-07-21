#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <math.h>
#include <string.h>

#include "espnow_battery_packet.h"

#ifndef ESP_NOW_WIFI_CHANNEL
#define ESP_NOW_WIFI_CHANNEL 1
#endif

#ifndef CYD_STANDALONE_TEST
#define CYD_STANDALONE_TEST 0
#endif

#ifndef TOUCH_CS
#define TOUCH_CS 33
#endif

#ifndef TOUCH_SCLK_PIN
#define TOUCH_SCLK_PIN 25
#endif

#ifndef TOUCH_MISO_PIN
#define TOUCH_MISO_PIN 39
#endif

#ifndef TOUCH_MOSI_PIN
#define TOUCH_MOSI_PIN 32
#endif

#ifndef TOUCH_IRQ_PIN
#define TOUCH_IRQ_PIN 36
#endif

#ifndef TOUCH_Z_THRESHOLD
#define TOUCH_Z_THRESHOLD 350
#endif

TFT_eSPI tft;
SPIClass touchSpi(HSPI);

constexpr int16_t kScreenW = 320;
constexpr int16_t kScreenH = 240;
constexpr int16_t kHeaderH = 32;
constexpr int16_t kAlarmY = 34;
constexpr int16_t kAlarmH = 18;
constexpr int16_t kFooterY = 210;
constexpr int16_t kFooterH = 30;
constexpr int16_t kPageButtonPin = 0;
constexpr int16_t kBottomPanelX = 16;
constexpr int16_t kBottomPanelY = 143;
constexpr int16_t kBottomPanelW = 286;
constexpr int16_t kBottomPanelH = 58;
constexpr uint8_t kPageCount = 4;
constexpr uint32_t kTouchDebounceMs = 260;
constexpr uint16_t kTouchPressureThreshold = TOUCH_Z_THRESHOLD;

constexpr uint16_t kBg = TFT_BLACK;
constexpr uint16_t kHeader = 0x0841;
constexpr uint16_t kPanel = 0x18E3;
constexpr uint16_t kPanelAlt = 0x2126;
constexpr uint16_t kPanelDark = 0x1082;
constexpr uint16_t kText = TFT_WHITE;
constexpr uint16_t kMuted = 0x9CD3;
constexpr uint16_t kCyan = 0x04FF;
constexpr uint16_t kGreen = 0x07E0;
constexpr uint16_t kAmber = 0xFDA0;
constexpr uint16_t kRed = 0xF800;
constexpr uint16_t kOk = kGreen;
constexpr uint16_t kFault = kRed;

struct BatterySample {
  float socPercent;
  float voltage;
  float current;
  float power;
  float temperature;
  float minCell;
  float maxCell;
  uint8_t cells;
  uint8_t satellites;
  bool modemOnline;
  bool loraOnline;
  bool rs485Online;
  bool sdOnline;
  bool packetLive;
  uint32_t sequence;
  uint8_t deviceId;
  uint16_t statusFlags;
  uint16_t faultFlags;
};

enum class FlowState {
  Charging,
  Discharging,
  Idle,
};

static BatterySample sample;
static uint8_t page = 0;
static uint32_t lastSampleMs = 0;
static uint32_t lastDrawMs = 0;
static bool lastButtonLevel = HIGH;
static bool lastTouchActive = false;
static uint32_t lastTouchMs = 0;
static bool touchReady = false;
static EspNowBatteryPacket latestPacket = {};
static volatile bool latestPacketPending = false;
static portMUX_TYPE packetMux = portMUX_INITIALIZER_UNLOCKED;
static uint8_t currentWifiChannel = ESP_NOW_WIFI_CHANNEL;
static uint32_t lastChannelHopMs = 0;

static void updateDynamicAreas();

static uint16_t socColor(float soc)
{
  if (soc < 20.0f) {
    return kRed;
  }
  if (soc < 45.0f) {
    return kAmber;
  }
  return kOk;
}

static FlowState flowState(float current)
{
  if (current > 0.6f) {
    return FlowState::Charging;
  }
  if (current < -0.6f) {
    return FlowState::Discharging;
  }
  return FlowState::Idle;
}

static const char *flowLabel(FlowState state)
{
  switch (state) {
    case FlowState::Charging:
      return "CARGA";
    case FlowState::Discharging:
      return "DESCARGA";
    case FlowState::Idle:
    default:
      return "REPOUSO";
  }
}

static uint16_t flowColor(FlowState state)
{
  switch (state) {
    case FlowState::Charging:
      return kOk;
    case FlowState::Discharging:
      return kAmber;
    case FlowState::Idle:
    default:
      return kOk;
  }
}

static bool packetIsValid(const uint8_t *data, int len)
{
  if (data == nullptr || len != sizeof(EspNowBatteryPacket)) {
    return false;
  }

  const EspNowBatteryPacket *packet = reinterpret_cast<const EspNowBatteryPacket *>(data);
  return packet->magic == ESP_NOW_BATTERY_MAGIC &&
         packet->protocolVersion == ESP_NOW_BATTERY_PROTOCOL_VERSION &&
         packet->packetSize == sizeof(EspNowBatteryPacket);
}

static void onEspNowRecv(const uint8_t *macAddr, const uint8_t *data, int len)
{
  (void)macAddr;

  if (!packetIsValid(data, len)) {
    return;
  }

  portENTER_CRITICAL_ISR(&packetMux);
  memcpy(&latestPacket, data, sizeof(latestPacket));
  latestPacketPending = true;
  portEXIT_CRITICAL_ISR(&packetMux);
}

static bool takeLatestPacket(EspNowBatteryPacket *packet)
{
  if (packet == nullptr || !latestPacketPending) {
    return false;
  }

  portENTER_CRITICAL(&packetMux);
  memcpy(packet, &latestPacket, sizeof(*packet));
  latestPacketPending = false;
  portEXIT_CRITICAL(&packetMux);
  return true;
}

static void setEspNowChannel(uint8_t channel)
{
  if (channel < 1) {
    channel = 1;
  } else if (channel > 13) {
    channel = 13;
  }

  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  currentWifiChannel = channel;
}

static void hopEspNowChannelIfStale()
{
#if !CYD_STANDALONE_TEST
  const uint32_t now = millis();
  if (lastSampleMs != 0 && now - lastSampleMs < 6000) {
    return;
  }
  if (now - lastChannelHopMs < 700) {
    return;
  }

  lastChannelHopMs = now;
  uint8_t nextChannel = currentWifiChannel >= 13 ? 1 : currentWifiChannel + 1;
  setEspNowChannel(nextChannel);
#endif
}

static bool beginEspNowReceiver()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(false, true);
  setEspNowChannel(ESP_NOW_WIFI_CHANNEL);

  esp_err_t result = esp_now_init();
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW init failed: %d\n", result);
    return false;
  }

  esp_now_register_recv_cb(onEspNowRecv);
  Serial.printf("ESP-NOW receiver ready, scanning from channel %u\n", currentWifiChannel);
  return true;
}

static void drawHeader()
{
  tft.fillRect(0, 0, kScreenW, kHeaderH, kHeader);
  tft.setTextDatum(TL_DATUM);
  tft.setTextColor(kText, kHeader);
  tft.setTextSize(2);
  tft.drawString("SOLMAR BMS", 12, 8);

  tft.setTextDatum(TR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(kOk, kHeader);
  tft.drawString("SIMULACAO", kScreenW - 12, 6);
  tft.setTextColor(kMuted, kHeader);
  tft.drawString("CYD display", kScreenW - 12, 20);
  tft.setTextDatum(TL_DATUM);
}

static void drawStaticLayout()
{
  tft.fillScreen(kBg);
  drawHeader();
  tft.drawRoundRect(16, 62, 124, 56, 8, kMuted);
  tft.fillRoundRect(140, 78, 8, 24, 3, kMuted);
  tft.setTextSize(1);
  tft.setTextColor(kMuted, kBg);
  tft.drawString("SOC", 18, 121);
  tft.drawString("Banco 16S LiFePO4", 18, 132);
}

static void drawAlarmBar(uint32_t ageMs)
{
  const char *text = "SEM ALARMES";
  uint16_t color = kOk;

  if (!sample.packetLive || ageMs > 5000) {
    text = "SEM DADOS DO GATEWAY";
    color = kFault;
  } else if (sample.faultFlags != 0) {
    text = "FALHA DA BATERIA";
    color = kFault;
  } else if (!sample.rs485Online) {
    text = "FALHA RS485";
    color = kFault;
  } else if (sample.socPercent < 20.0f) {
    text = "BATERIA BAIXA";
    color = kFault;
  } else if (!sample.loraOnline) {
    text = "LORA INTERMITENTE";
    color = kAmber;
  } else if (sample.temperature > 42.0f) {
    text = "TEMPERATURA ALTA";
    color = kAmber;
  }

  tft.fillRect(0, kAlarmY, kScreenW, kAlarmH, color);
  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(kBg, color);
  tft.drawString(text, kScreenW / 2, kAlarmY + 9);
  tft.setTextDatum(TL_DATUM);
}

static void drawBatteryGauge()
{
  const int16_t x = 16;
  const int16_t y = 62;
  const int16_t w = 124;
  const int16_t h = 56;
  const uint16_t color = socColor(sample.socPercent);

  tft.fillRoundRect(x + 3, y + 3, w - 6, h - 6, 6, kBg);
  const int16_t fillW = static_cast<int16_t>((w - 10) * constrain(sample.socPercent, 0.0f, 100.0f) / 100.0f);
  tft.fillRoundRect(x + 5, y + 5, fillW, h - 10, 5, color);
  tft.drawRoundRect(x, y, w, h, 8, kMuted);

  tft.setTextDatum(MC_DATUM);
  tft.setTextSize(3);
  tft.setTextColor(kText);
  tft.drawString(String(static_cast<int>(sample.socPercent + 0.5f)) + "%", x + w / 2, y + h / 2);
  tft.setTextDatum(TL_DATUM);
}

static void drawPowerPanel()
{
  const int16_t x = 158;
  const int16_t y = 58;
  const int16_t w = 144;
  const int16_t h = 76;
  const FlowState state = flowState(sample.current);
  const uint16_t accent = flowColor(state);

  tft.fillRoundRect(x, y, w, h, 7, kPanel);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(kMuted, kPanel);
  tft.drawString("Potencia", x + 12, y + 9);

  tft.setTextSize(3);
  tft.setTextColor(accent, kPanel);
  tft.drawString(String(static_cast<int>(fabsf(sample.power))), x + 12, y + 28);

  tft.setTextSize(2);
  tft.drawString("W", x + 92, y + 39);

  tft.fillRoundRect(x + 12, y + 58, 86, 14, 7, kPanelDark);
  tft.setTextSize(1);
  tft.setTextColor(accent, kPanelDark);
  tft.drawString(flowLabel(state), x + 23, y + 61);
  if (state == FlowState::Charging) {
    tft.fillTriangle(x + 16, y + 66, x + 20, y + 60, x + 24, y + 66, accent);
  } else if (state == FlowState::Discharging) {
    tft.fillTriangle(x + 16, y + 60, x + 20, y + 66, x + 24, y + 60, accent);
  } else {
    tft.fillRect(x + 16, y + 63, 8, 2, accent);
  }
}

static void drawMetricCard(int16_t x, int16_t y, int16_t w, const char *label, const String &value,
                           const char *unit, uint16_t accent)
{
  tft.fillRoundRect(x, y, w, 48, 6, kPanelAlt);
  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(kMuted, kPanelAlt);
  tft.drawString(label, x + 8, y + 7);

  tft.setTextSize(2);
  tft.setTextColor(accent, kPanelAlt);
  tft.drawString(value, x + 8, y + 24);

  tft.setTextSize(1);
  tft.setTextColor(kMuted, kPanelAlt);
  tft.drawString(unit, x + w - 22, y + 31);
}

static void drawStatusPill(int16_t x, int16_t y, const char *label, bool ok)
{
  const uint16_t color = ok ? kOk : kFault;
  tft.fillRoundRect(x, y, 62, 18, 9, kPanelDark);
  tft.fillCircle(x + 10, y + 9, 4, color);
  tft.setTextSize(1);
  tft.setTextColor(kText, kPanelDark);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(label, x + 20, y + 9);
  tft.setTextDatum(TL_DATUM);
}

static void drawFooter(uint32_t ageMs)
{
  tft.fillRect(0, kFooterY, kScreenW, kFooterH, kHeader);
  drawStatusPill(10, 216, "RS485", sample.rs485Online);
  drawStatusPill(80, 216, "4G", sample.modemOnline);
  drawStatusPill(150, 216, "LORA", sample.loraOnline);

  tft.setTextDatum(TR_DATUM);
  tft.setTextSize(1);
  tft.setTextColor(ageMs > 5000 ? kFault : kOk, kHeader);
  tft.drawString(ageMs > 5000 ? "DADO VELHO" : "AO VIVO", kScreenW - 10, 215);
  tft.setTextColor(kMuted, kHeader);
  tft.drawString(String(ageMs / 1000) + "s  CH " + String(currentWifiChannel), kScreenW - 10, 228);
  tft.setTextDatum(TL_DATUM);
}

static void drawPagePanel()
{
  const int16_t x = kBottomPanelX;
  const int16_t y = kBottomPanelY;
  const int16_t w = kBottomPanelW;
  const int16_t h = kBottomPanelH;

  tft.fillRoundRect(x, y, w, h, 6, kPanelAlt);
  tft.setTextSize(1);
  tft.setTextColor(kMuted, kPanelAlt);
  tft.drawString("TOQUE PARA TROCAR", x + w - 104, y + 7);

  if (page == 0) {
    tft.drawString("Resumo", x + 8, y + 7);
    tft.setTextSize(2);
    tft.setTextColor(kOk, kPanelAlt);
    tft.drawString(String(sample.voltage, 1) + "V", x + 8, y + 23);
    tft.setTextColor(flowColor(flowState(sample.current)), kPanelAlt);
    tft.drawString(String(sample.current, 1) + "A", x + 92, y + 23);
    tft.setTextColor(kAmber, kPanelAlt);
    tft.drawString(String(static_cast<int>(fabsf(sample.power))) + "W", x + 190, y + 23);
  } else if (page == 1) {
    tft.drawString("Comunicacao", x + 8, y + 7);
    tft.setTextColor(sample.modemOnline ? kOk : kFault, kPanelAlt);
    tft.drawString("4G   " + String(sample.modemOnline ? "OK" : "FALHA"), x + 8, y + 24);
    tft.setTextColor(sample.sdOnline ? kOk : kFault, kPanelAlt);
    tft.drawString("SD   " + String(sample.sdOnline ? "OK" : "FALHA"), x + 8, y + 38);
    tft.setTextColor(sample.loraOnline ? kOk : kFault, kPanelAlt);
    tft.drawString("LORA " + String(sample.loraOnline ? "OK" : "FALHA"), x + 120, y + 24);
    tft.setTextColor(sample.rs485Online ? kOk : kFault, kPanelAlt);
    tft.drawString("RS485 " + String(sample.rs485Online ? "OK" : "FALHA"), x + 120, y + 38);
  } else if (page == 2) {
    tft.drawString("Celulas", x + 8, y + 7);
    tft.setTextColor(kOk, kPanelAlt);
    tft.drawString("MIN " + String(sample.minCell, 3) + "V", x + 8, y + 24);
    tft.setTextColor(kOk, kPanelAlt);
    tft.drawString("MAX " + String(sample.maxCell, 3) + "V", x + 8, y + 38);
    tft.setTextColor(kMuted, kPanelAlt);
    tft.drawString(String(sample.cells) + " celulas", x + 176, y + 31);
  } else {
    tft.drawString("GPS / enlace", x + 8, y + 7);
    tft.setTextColor(sample.satellites > 0 ? kOk : kAmber, kPanelAlt);
    tft.drawString("SAT " + String(sample.satellites), x + 8, y + 24);
    tft.setTextColor(sample.packetLive ? kOk : kFault, kPanelAlt);
    tft.drawString("PKT " + String(sample.packetLive ? "OK" : "VELHO"), x + 8, y + 38);
    tft.setTextColor(kMuted, kPanelAlt);
    tft.drawString("SEQ " + String(sample.sequence), x + 120, y + 24);
    tft.drawString("BMS " + String(sample.deviceId), x + 120, y + 38);
  }

  const int16_t dotY = y + h - 7;
  for (uint8_t i = 0; i < kPageCount; ++i) {
    tft.fillCircle(x + w - 42 + i * 11, dotY, 3, i == page ? kOk : kMuted);
  }
}

static BatterySample fakeSample()
{
  const float t = millis() / 1000.0f;
  BatterySample next{};

  next.socPercent = 76.0f + sinf(t * 0.25f) * 3.0f;
  next.voltage = 52.4f + sinf(t * 0.8f) * 0.35f;
  next.current = -12.8f + sinf(t * 0.55f) * 2.1f;
  next.power = next.voltage * next.current;
  next.temperature = 31.0f + sinf(t * 0.35f) * 1.7f;
  next.minCell = 3.267f + sinf(t * 0.31f) * 0.006f;
  next.maxCell = 3.286f + sinf(t * 0.29f) * 0.006f;
  next.cells = 16;
  next.satellites = 7 + static_cast<uint8_t>((millis() / 3000) % 3);
  next.modemOnline = true;
  next.loraOnline = ((millis() / 5000) % 6) != 0;
  next.rs485Online = true;
  next.sdOnline = true;
  next.packetLive = true;
  next.sequence = millis() / 1000;
  next.deviceId = 1;
  next.statusFlags = ESP_NOW_BATTERY_STATUS_CHARGE_ENABLE |
                     ESP_NOW_BATTERY_STATUS_DISCHARGE_ENABLE;
  next.faultFlags = 0;
  return next;
}

static BatterySample sampleFromPacket(const EspNowBatteryPacket &packet)
{
  BatterySample next{};

  next.socPercent = constrain(static_cast<float>(packet.socPercent), 0.0f, 100.0f);
  next.voltage = packet.voltageV;
  next.current = packet.currentA;
  next.power = packet.packPowerW;
  next.temperature = static_cast<float>(packet.temperatureC);
  next.minCell = 0.0f;
  next.maxCell = 0.0f;
  next.cells = 16;
  next.satellites = 0;
  next.modemOnline = true;
  next.loraOnline = true;
  next.rs485Online = true;
  next.sdOnline = true;
  next.packetLive = true;
  next.sequence = packet.sequence;
  next.deviceId = packet.deviceId;
  next.statusFlags = packet.statusFlags;
  next.faultFlags = packet.faultFlags;
  return next;
}

static void applyPacket(const EspNowBatteryPacket &packet)
{
  sample = sampleFromPacket(packet);
  lastSampleMs = millis();
  updateDynamicAreas();
  Serial.printf("ESP-NOW BMS%u seq=%lu ch=%u V=%.2f I=%.2f P=%.1f SOC=%.0f T=%.0f faults=0x%04X\n",
                sample.deviceId,
                (unsigned long)sample.sequence,
                currentWifiChannel,
                sample.voltage,
                sample.current,
                sample.power,
                sample.socPercent,
                sample.temperature,
                sample.faultFlags);
}

static void updateDynamicAreas()
{
  const uint32_t ageMs = millis() - lastSampleMs;

  tft.startWrite();
  drawAlarmBar(ageMs);
  drawBatteryGauge();
  drawPowerPanel();
  drawPagePanel();
  drawFooter(ageMs);
  tft.endWrite();
}

static void nextPage()
{
  page = (page + 1) % kPageCount;
  tft.startWrite();
  drawPagePanel();
  tft.endWrite();
}

static void handlePageButton()
{
  const bool level = digitalRead(kPageButtonPin);
  if (lastButtonLevel == HIGH && level == LOW) {
    nextPage();
    delay(180);
  }
  lastButtonLevel = level;
}

static uint16_t readTouch12(uint8_t command)
{
  touchSpi.transfer(command);
  const uint16_t high = touchSpi.transfer(0x00);
  const uint16_t low = touchSpi.transfer(0x00);
  return ((high << 8) | low) >> 3;
}

static bool xpt2046Touched()
{
  if (!touchReady) {
    return false;
  }

  const bool irqPressed = TOUCH_IRQ_PIN >= 0 && digitalRead(TOUCH_IRQ_PIN) == LOW;

  touchSpi.beginTransaction(SPISettings(SPI_TOUCH_FREQUENCY, MSBFIRST, SPI_MODE0));
  digitalWrite(TOUCH_CS, LOW);

  const int16_t z1 = readTouch12(0xB1);
  const int16_t z2 = readTouch12(0xC1);
  readTouch12(0x91);
  touchSpi.transfer(0x00);

  digitalWrite(TOUCH_CS, HIGH);
  touchSpi.endTransaction();

  int16_t pressure = z1 + 4095 - z2;
  if (pressure < 0) {
    pressure = 0;
  }

  return irqPressed || pressure >= kTouchPressureThreshold;
}

static void handleTouch()
{
  const bool touched = xpt2046Touched();
  const uint32_t now = millis();

  if (touched && !lastTouchActive && now - lastTouchMs >= kTouchDebounceMs) {
    lastTouchMs = now;
    nextPage();
    Serial.printf("Touch page=%u\n", page);
  }

  lastTouchActive = touched;
}

void setup()
{
  Serial.begin(115200);
  delay(200);

  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, TFT_BACKLIGHT_ON);
  pinMode(kPageButtonPin, INPUT_PULLUP);

  tft.init();
  tft.setRotation(1);
  tft.setTextFont(1);
  tft.setSwapBytes(true);

  pinMode(TOUCH_CS, OUTPUT);
  digitalWrite(TOUCH_CS, HIGH);
  if (TOUCH_IRQ_PIN >= 0) {
    pinMode(TOUCH_IRQ_PIN, INPUT);
  }
  touchSpi.begin(TOUCH_SCLK_PIN, TOUCH_MISO_PIN, TOUCH_MOSI_PIN, TOUCH_CS);
  touchReady = true;
  Serial.printf("Touch XPT2046 ready CS=%d CLK=%d MISO=%d MOSI=%d IRQ=%d Z_MIN=%u\n",
                TOUCH_CS,
                TOUCH_SCLK_PIN,
                TOUCH_MISO_PIN,
                TOUCH_MOSI_PIN,
                TOUCH_IRQ_PIN,
                kTouchPressureThreshold);

  sample = fakeSample();
  sample.packetLive = CYD_STANDALONE_TEST != 0;
  lastSampleMs = CYD_STANDALONE_TEST ? millis() : 0;

#if !CYD_STANDALONE_TEST
  beginEspNowReceiver();
#endif

  drawStaticLayout();
  updateDynamicAreas();
  Serial.println(CYD_STANDALONE_TEST ? "CYD battery screen demo started" : "CYD battery screen ESP-NOW receiver started");
}

void loop()
{
  handlePageButton();
  handleTouch();

  const uint32_t now = millis();
#if CYD_STANDALONE_TEST
  if (now - lastDrawMs >= 1000) {
    lastDrawMs = now;
    sample = fakeSample();
    lastSampleMs = now;
    updateDynamicAreas();
    Serial.printf("SOC=%.1f V=%.1f I=%.1f P=%.0f T=%.1f page=%u\n",
                  sample.socPercent,
                  sample.voltage,
                  sample.current,
                  sample.power,
                  sample.temperature,
                  page);
  }
#else
  EspNowBatteryPacket packet;
  if (takeLatestPacket(&packet)) {
    applyPacket(packet);
  }

  hopEspNowChannelIfStale();

  if (now - lastDrawMs >= 1000) {
    lastDrawMs = now;
    if (lastSampleMs == 0 || now - lastSampleMs > 5000) {
      sample.packetLive = false;
    }
    updateDynamicAreas();
  }
#endif
}
