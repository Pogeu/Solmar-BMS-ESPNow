#include <Arduino.h>
#include <Wire.h>

#define USE_SERIAL_1602_LCD
#include <LCDBigNumbers.hpp>

#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

#include "espnow_battery_packet.h"

#ifndef ESP_NOW_WIFI_CHANNEL
#define ESP_NOW_WIFI_CHANNEL 1
#endif

#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif

#ifndef LCD_COLUMNS
#define LCD_COLUMNS 16
#endif

#ifndef LCD_ROWS
#define LCD_ROWS 2
#endif

#ifndef LCD_SDA_PIN
#define LCD_SDA_PIN 8
#endif

#ifndef LCD_SCL_PIN
#define LCD_SCL_PIN 9
#endif

#ifndef LCD_PAGE_BUTTON_PIN
#define LCD_PAGE_BUTTON_PIN 4
#endif

#ifndef LCD_BUTTON_DEBOUNCE_MS
#define LCD_BUTTON_DEBOUNCE_MS 80
#endif

constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t PACKET_STALE_AFTER_MS = 10000;
constexpr uint8_t DISPLAY_PAGE_COUNT = 5;

#if defined(LCD_STANDALONE_TEST)
constexpr uint32_t DEMO_PACKET_INTERVAL_MS = 1000;
#endif

static_assert(sizeof(EspNowBatteryPacket) <= ESP_NOW_MAX_DATA_LEN,
              "ESP-NOW battery packet exceeds maximum payload size");

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLUMNS, LCD_ROWS);
LCDBigNumbers bigNumberLCD(&lcd, BIG_NUMBERS_FONT_2_COLUMN_2_ROWS_VARIANT_1);

QueueHandle_t packetQueue;
EspNowBatteryPacket latestPacket = {};
uint32_t latestPacketAtMs = 0;
uint32_t latestDisplayUpdateMs = 0;
char displayedLines[LCD_ROWS][LCD_COLUMNS + 1] = {};
uint8_t displayPage = 0;
volatile bool pageButtonInterruptSeen = false;
bool pageButtonStableState = HIGH;
uint32_t pageButtonLastChangeMs = 0;
bool mainPageRendered = false;
char displayedMainSoc[4] = {};
char displayedMainPower[5] = {};
char displayedMainPowerUnit = '\0';
bool displayedMainStale = false;

#if defined(LCD_STANDALONE_TEST)
uint32_t demoPacketUpdateMs = 0;
uint32_t demoSequence = 0;
#endif

struct VoltageSocPoint {
  float voltage;
  uint8_t socPercent;
};

// Approximate 48 V LiFePO4 resting voltage chart from Jackery.
const VoltageSocPoint LIFEPO4_48V_SOC_TABLE[] = {
  {54.40f, 100},
  {53.60f, 90},
  {53.12f, 80},
  {52.80f, 70},
  {52.32f, 60},
  {52.16f, 50},
  {52.00f, 40},
  {51.52f, 30},
  {51.20f, 20},
  {48.00f, 10},
  {40.00f, 0},
};

static void IRAM_ATTR onPageButtonInterrupt()
{
  pageButtonInterruptSeen = true;
}

static void clearDisplayedLines()
{
  memset(displayedLines, 0, sizeof(displayedLines));
  memset(displayedMainSoc, 0, sizeof(displayedMainSoc));
  memset(displayedMainPower, 0, sizeof(displayedMainPower));
  displayedMainPowerUnit = '\0';
  displayedMainStale = false;
  mainPageRendered = false;
}

static void lcdPrintLine(uint8_t row, const char *text)
{
  if (row >= LCD_ROWS) {
    return;
  }

  char padded[LCD_COLUMNS + 1];
  size_t len = strnlen(text, LCD_COLUMNS);

  memset(padded, ' ', LCD_COLUMNS);
  memcpy(padded, text, len);
  padded[LCD_COLUMNS] = '\0';

  if (strncmp(displayedLines[row], padded, LCD_COLUMNS) == 0) {
    return;
  }

  lcd.setCursor(0, row);
  lcd.print(padded);
  memcpy(displayedLines[row], padded, LCD_COLUMNS + 1);
}

static void lcdClear()
{
  lcd.clear();
  clearDisplayedLines();
}

static const char *onOffText(bool enabled)
{
  return enabled ? "ON" : "OFF";
}

static uint32_t cappedAgeSeconds(uint32_t ageMs)
{
  uint32_t ageSeconds = ageMs / 1000;
  return ageSeconds > 999 ? 999 : ageSeconds;
}

static uint16_t cappedSocPercent(uint16_t socPercent)
{
  return socPercent > 100 ? 100 : socPercent;
}

static uint8_t lifepo4SocFromRestingVoltage(float voltage)
{
  const size_t pointCount = sizeof(LIFEPO4_48V_SOC_TABLE) / sizeof(LIFEPO4_48V_SOC_TABLE[0]);

  if (voltage >= LIFEPO4_48V_SOC_TABLE[0].voltage) {
    return LIFEPO4_48V_SOC_TABLE[0].socPercent;
  }

  for (size_t i = 1; i < pointCount; i++) {
    const VoltageSocPoint &high = LIFEPO4_48V_SOC_TABLE[i - 1];
    const VoltageSocPoint &low = LIFEPO4_48V_SOC_TABLE[i];

    if (voltage <= high.voltage && voltage >= low.voltage) {
      float voltageSpan = high.voltage - low.voltage;
      float ratio = voltageSpan > 0.0f ? (voltage - low.voltage) / voltageSpan : 0.0f;
      return low.socPercent + (uint8_t)roundf(ratio * (high.socPercent - low.socPercent));
    }
  }

  return LIFEPO4_48V_SOC_TABLE[pointCount - 1].socPercent;
}

static long roundedWatts(float watts)
{
  return watts >= 0.0f ? (long)(watts + 0.5f) : (long)(watts - 0.5f);
}

static long displayWatts(float watts)
{
  long rounded = roundedWatts(watts);

  if (rounded > 9999) {
    rounded = 9999;
  } else if (rounded < -9999) {
    rounded = -9999;
  }

  return rounded;
}

static char formatBigPower(float watts, char *text, size_t textSize)
{
  long rounded = roundedWatts(watts);

  if (rounded >= 1000 || rounded <= -1000) {
    float kilowatts = watts / 1000.0f;

    if (kilowatts > 9.9f) {
      kilowatts = 9.9f;
    } else if (kilowatts < -9.0f) {
      kilowatts = -9.0f;
    }

    snprintf(text, textSize, "%3.1f", kilowatts);
    return 'k';
  }

  if (rounded > 999) {
    rounded = 999;
  } else if (rounded < -99) {
    rounded = -99;
  }

  snprintf(text, textSize, "%3ld", rounded);
  return 'W';
}

static void setupDisplayBus()
{
#if defined(ARDUINO_ARCH_ESP32)
  Wire.setPins(LCD_SDA_PIN, LCD_SCL_PIN);
#else
  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
#endif
}

static void setupPageButton()
{
  pinMode(LCD_PAGE_BUTTON_PIN, INPUT_PULLUP);
  pageButtonStableState = digitalRead(LCD_PAGE_BUTTON_PIN);
  attachInterrupt(digitalPinToInterrupt(LCD_PAGE_BUTTON_PIN), onPageButtonInterrupt, CHANGE);
}

static void forceDisplayRefresh()
{
  latestDisplayUpdateMs = 0;
  lcdClear();
}

static void pollPageButton()
{
  if (!pageButtonInterruptSeen) {
    return;
  }

  noInterrupts();
  pageButtonInterruptSeen = false;
  interrupts();

  bool reading = digitalRead(LCD_PAGE_BUTTON_PIN);
  uint32_t now = millis();

  if (now - pageButtonLastChangeMs < LCD_BUTTON_DEBOUNCE_MS) {
    return;
  }

  if (reading == pageButtonStableState) {
    return;
  }

  pageButtonStableState = reading;
  pageButtonLastChangeMs = now;

  if (pageButtonStableState == LOW) {
    displayPage = (displayPage + 1) % DISPLAY_PAGE_COUNT;
    forceDisplayRefresh();
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

  EspNowBatteryPacket packet;
  memcpy(&packet, data, sizeof(packet));
  xQueueOverwrite(packetQueue, &packet);
}

static void showStartupScreen()
{
  lcdClear();
  lcdPrintLine(0, "ESP-NOW BMS RX");
  lcdPrintLine(1, "Aguardando...");
}

static void showWaitingScreen()
{
  lcdPrintLine(0, "ESP-NOW BMS RX");
  lcdPrintLine(1, "Sem dados");
}

#if defined(LCD_STANDALONE_TEST)
static EspNowBatteryPacket makeDemoPacket(uint32_t sequence)
{
  EspNowBatteryPacket packet = {};
  uint8_t phase = sequence % 24;
  uint16_t soc = phase < 12 ? 35 + phase : 47 - (phase - 12);
  float current = 2.06f + (phase % 5) * 0.18f;
  float voltage = 52.4f + (phase % 4) * 0.1f;

  packet.magic = ESP_NOW_BATTERY_MAGIC;
  packet.protocolVersion = ESP_NOW_BATTERY_PROTOCOL_VERSION;
  packet.packetSize = sizeof(EspNowBatteryPacket);
  packet.deviceId = 1;
  packet.sequence = sequence;
  packet.uptimeMs = millis();
  packet.voltageV = voltage;
  packet.currentA = current;
  packet.packPowerW = voltage * current;
  packet.socPercent = soc;
  packet.temperatureC = 22 + (phase % 4);
  packet.statusFlags = ESP_NOW_BATTERY_STATUS_CHARGE_ENABLE |
                       ESP_NOW_BATTERY_STATUS_DISCHARGE_ENABLE;
  packet.faultFlags = (phase >= 18) ? ESP_NOW_BATTERY_FAULT_BMS_TEMPERATURE_HIGH : 0;

  return packet;
}

static void updateDemoPacket()
{
  uint32_t now = millis();

  if (latestPacketAtMs != 0 && now - demoPacketUpdateMs < DEMO_PACKET_INTERVAL_MS) {
    return;
  }

  demoPacketUpdateMs = now;
  latestPacket = makeDemoPacket(demoSequence++);
  latestPacketAtMs = now;

  Serial.printf("LCD demo BMS%u V=%.2f I=%.2f P=%.1f SOC=%u T=%u faults=0x%04X seq=%lu\n",
                latestPacket.deviceId,
                latestPacket.voltageV,
                latestPacket.currentA,
                latestPacket.packPowerW,
                latestPacket.socPercent,
                latestPacket.temperatureC,
                latestPacket.faultFlags,
                (unsigned long)latestPacket.sequence);
}
#endif

static void showMainPage(const EspNowBatteryPacket &packet, bool stale)
{
  char socText[4];
  char powerText[5];
  uint16_t socPercent = cappedSocPercent(packet.socPercent);
  char powerUnit = formatBigPower(packet.packPowerW, powerText, sizeof(powerText));

  snprintf(socText, sizeof(socText), "%3u", socPercent);

  if (!mainPageRendered) {
    lcdClear();
  }

  if (mainPageRendered &&
      strcmp(displayedMainSoc, socText) == 0 &&
      strcmp(displayedMainPower, powerText) == 0 &&
      displayedMainPowerUnit == powerUnit &&
      displayedMainStale == stale) {
    return;
  }

  bigNumberLCD.begin();
  bigNumberLCD.disableGapBetweenNumbers();
  bigNumberLCD.setBigNumberCursor(0, 0);
  bigNumberLCD.print(socText);
  lcd.setCursor(6, 1);
  lcd.print(stale ? '!' : '%');

  bigNumberLCD.setBigNumberCursor(9, 0);
  bigNumberLCD.print(powerText);
  lcd.setCursor(15, 1);
  lcd.print(powerUnit);

  memcpy(displayedMainSoc, socText, sizeof(displayedMainSoc));
  memcpy(displayedMainPower, powerText, sizeof(displayedMainPower));
  displayedMainPowerUnit = powerUnit;
  displayedMainStale = stale;
  mainPageRendered = true;
}

static void showElectricalPage(const EspNowBatteryPacket &packet)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];
  uint16_t socPercent = cappedSocPercent(packet.socPercent);

  snprintf(line0, sizeof(line0), "V%5.2f I%5.1f", packet.voltageV, packet.currentA);
  snprintf(line1, sizeof(line1), "P%6ldW S%3u%%", displayWatts(packet.packPowerW), socPercent);

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showLifepo4SocPage(const EspNowBatteryPacket &packet)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];
  uint8_t voltageSoc = lifepo4SocFromRestingVoltage(packet.voltageV);
  uint16_t bmsSoc = cappedSocPercent(packet.socPercent);

  snprintf(line0, sizeof(line0), "BMS%3u%% Volt%3u%%", bmsSoc, voltageSoc);
  snprintf(line1, sizeof(line1), "48V OCV %5.2f", packet.voltageV);

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showStatusPage(const EspNowBatteryPacket &packet)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];

  snprintf(line0, sizeof(line0), "B%u Temp %3uC", packet.deviceId, packet.temperatureC);
  snprintf(line1, sizeof(line1), "C:%-3s D:%-3s",
           onOffText(packet.statusFlags & ESP_NOW_BATTERY_STATUS_CHARGE_ENABLE),
           onOffText(packet.statusFlags & ESP_NOW_BATTERY_STATUS_DISCHARGE_ENABLE));

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showFaultLinkPage(const EspNowBatteryPacket &packet, uint32_t ageMs, bool stale)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];

  if (packet.faultFlags != 0) {
    snprintf(line0, sizeof(line0), "FALHA 0x%04X", packet.faultFlags);
  } else {
    snprintf(line0, sizeof(line0), "Sem falhas");
  }

  snprintf(line1, sizeof(line1), "Link %s %3lus",
           stale ? "OLD" : "OK ",
           (unsigned long)cappedAgeSeconds(ageMs));

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showBatteryScreen(const EspNowBatteryPacket &packet, uint32_t ageMs)
{
  bool stale = ageMs > PACKET_STALE_AFTER_MS;

  switch (displayPage) {
    case 0:
      showMainPage(packet, stale);
      break;

    case 1:
      showElectricalPage(packet);
      break;

    case 2:
      showLifepo4SocPage(packet);
      break;

    case 3:
      showStatusPage(packet);
      break;

    default:
      showFaultLinkPage(packet, ageMs, stale);
      break;
  }
}

static bool startEspNow()
{
  WiFi.mode(WIFI_STA);
  WiFi.disconnect(true);

  esp_err_t result = esp_wifi_set_channel(ESP_NOW_WIFI_CHANNEL, WIFI_SECOND_CHAN_NONE);
  if (result != ESP_OK) {
    Serial.printf("Failed to set WiFi channel %d: %d\n", ESP_NOW_WIFI_CHANNEL, result);
    return false;
  }

  result = esp_now_init();
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW init failed: %d\n", result);
    return false;
  }

  result = esp_now_register_recv_cb(onEspNowRecv);
  if (result != ESP_OK) {
    Serial.printf("ESP-NOW receive callback failed: %d\n", result);
    return false;
  }

  Serial.printf("ESP-NOW receiver ready on channel %d\n", ESP_NOW_WIFI_CHANNEL);
  Serial.print("Receiver MAC: ");
  Serial.println(WiFi.macAddress());
  return true;
}

void setup()
{
  Serial.begin(115200);
  delay(500);

  setupDisplayBus();
  setupPageButton();
  lcd.init();
  lcd.backlight();
  bigNumberLCD.begin();
  bigNumberLCD.disableGapBetweenNumbers();
  showStartupScreen();

#if defined(LCD_STANDALONE_TEST)
  Serial.println("LCD standalone test mode");
  lcdPrintLine(0, "LCD page test");
  lcdPrintLine(1, "Botao troca pg");
#else
  packetQueue = xQueueCreate(1, sizeof(EspNowBatteryPacket));
  if (packetQueue == nullptr) {
    Serial.println("Failed to create packet queue");
    lcdPrintLine(1, "Erro fila");
    return;
  }

  if (!startEspNow()) {
    lcdPrintLine(0, "ESP-NOW erro");
    lcdPrintLine(1, "Ver Serial");
  }
#endif
}

void loop()
{
  pollPageButton();

#if defined(LCD_STANDALONE_TEST)
  updateDemoPacket();
#else
  EspNowBatteryPacket packet;
  if (packetQueue != nullptr && xQueueReceive(packetQueue, &packet, 0) == pdTRUE) {
    latestPacket = packet;
    latestPacketAtMs = millis();

    Serial.printf("BMS%u V=%.2f I=%.2f P=%.1f SOC=%u T=%u flags=0x%04X faults=0x%04X seq=%lu\n",
                  latestPacket.deviceId,
                  latestPacket.voltageV,
                  latestPacket.currentA,
                  latestPacket.packPowerW,
                  latestPacket.socPercent,
                  latestPacket.temperatureC,
                  latestPacket.statusFlags,
                  latestPacket.faultFlags,
                  (unsigned long)latestPacket.sequence);
  }
#endif

  if (millis() - latestDisplayUpdateMs >= DISPLAY_REFRESH_INTERVAL_MS) {
    latestDisplayUpdateMs = millis();

    if (latestPacketAtMs == 0) {
      showWaitingScreen();
    } else {
      showBatteryScreen(latestPacket, millis() - latestPacketAtMs);
    }
  }
}
