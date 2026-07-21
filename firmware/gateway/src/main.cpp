//
// Solmar BMS Gateway
//
#include <Arduino.h>
#include <math.h>

#if BMS_LCD_DIRECT_MODE
#include <SPI.h>
#include <U8g2lib.h>
#else
#include "espnow_battery.h"
#endif

#include "bms_sd_logger.h"
#include "bms_mqtt_publisher.h"
#include "bms_lora_publisher.h"
#include "bms_location.h"
#include "bms_ota.h"
#include "felicity.h"
#include "main.h"

#ifndef BMS_CENTRAL_MODE
#define BMS_CENTRAL_MODE 0
#endif

#ifndef BMS_DISPLAY_STANDALONE_TEST
#define BMS_DISPLAY_STANDALONE_TEST 0
#endif

#ifndef BMS_LOCATION_PUBLISH_INTERVAL_MS
#define BMS_LOCATION_PUBLISH_INTERVAL_MS 10000
#endif

#ifndef BMS_CENTRAL_DEMO_ESPNOW
#define BMS_CENTRAL_DEMO_ESPNOW 0
#endif

#ifndef BMS_CENTRAL_DEMO_ESPNOW_INTERVAL_MS
#define BMS_CENTRAL_DEMO_ESPNOW_INTERVAL_MS 1000
#endif

static void printSerialMessage(const BmsMessage &msg);

#if BMS_LCD_DIRECT_MODE
#ifndef DISPLAY_SPI_SCK_PIN
#define DISPLAY_SPI_SCK_PIN 18
#endif

#ifndef DISPLAY_SPI_MOSI_PIN
#define DISPLAY_SPI_MOSI_PIN 23
#endif

#ifndef DISPLAY_CS_PIN
#define DISPLAY_CS_PIN 15
#endif

#ifndef DISPLAY_DC_PIN
#define DISPLAY_DC_PIN 16
#endif

#ifndef DISPLAY_RESET_PIN
#define DISPLAY_RESET_PIN 17
#endif

#ifndef DISPLAY_PAGE_BUTTON_PIN
#define DISPLAY_PAGE_BUTTON_PIN 4
#endif

#ifndef DISPLAY_BUTTON_DEBOUNCE_MS
#define DISPLAY_BUTTON_DEBOUNCE_MS 80
#endif

#ifndef DISPLAY_CONTRAST
#define DISPLAY_CONTRAST -1
#endif

#ifndef DISPLAY_FILLED_PROGRESS_BAR
#define DISPLAY_FILLED_PROGRESS_BAR 1
#endif

#ifndef DISPLAY_USE_ERC12864_ALT
#define DISPLAY_USE_ERC12864_ALT 0
#endif

constexpr uint32_t DISPLAY_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t DISPLAY_BATTERY_STALE_AFTER_MS = 10000;
constexpr uint8_t DISPLAY_PAGE_COUNT = 5;
constexpr uint8_t DISPLAY_WIDTH = 128;
constexpr uint8_t DISPLAY_BAR_X = 6;
constexpr uint8_t DISPLAY_BAR_Y = 52;
constexpr uint8_t DISPLAY_BAR_WIDTH = 116;
constexpr uint8_t DISPLAY_BAR_HEIGHT = 8;

#if BMS_DISPLAY_STANDALONE_TEST
constexpr uint32_t DEMO_BMS_UPDATE_INTERVAL_MS = 1000;
uint32_t demoBmsUpdateMs = 0;
uint32_t demoBmsSequence = 0;
#endif

struct VoltageSocPoint {
  float voltage;
  uint8_t socPercent;
};

struct DirectDisplayState {
  BmsMessage battery = {};
  BmsMessage cells = {};
  bool hasBattery = false;
  bool hasCells = false;
  uint32_t batteryAtMs = 0;
};

#if DISPLAY_USE_ERC12864_ALT
U8G2_ST7565_ERC12864_ALT_F_4W_SW_SPI display(
    U8G2_R0,
    DISPLAY_SPI_SCK_PIN,
    DISPLAY_SPI_MOSI_PIN,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RESET_PIN);
#else
U8G2_ST7565_ERC12864_F_4W_SW_SPI display(
    U8G2_R0,
    DISPLAY_SPI_SCK_PIN,
    DISPLAY_SPI_MOSI_PIN,
    DISPLAY_CS_PIN,
    DISPLAY_DC_PIN,
    DISPLAY_RESET_PIN);
#endif

DirectDisplayState displayState;
uint32_t latestDisplayUpdateMs = 0;
uint8_t displayPage = 0;

static void printSerialMessage(const BmsMessage &msg);

volatile bool pageButtonInterruptSeen = false;
bool pageButtonStableState = HIGH;
uint32_t pageButtonLastChangeMs = 0;

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

static const char *powerUnitText(char unit)
{
  return unit == 'k' ? "kW" : "W";
}

static void formatPageStatus(char *text, size_t textSize, uint8_t pageIndex, bool stale)
{
  snprintf(text, textSize, stale ? "OLD %u/%u" : "%u/%u",
           (unsigned)(pageIndex + 1),
           (unsigned)DISPLAY_PAGE_COUNT);
}

static void drawHeader(const char *title, const char *status)
{
  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 8, title);

  if (status != nullptr && status[0] != '\0') {
    int16_t statusX = DISPLAY_WIDTH - (int16_t)display.getStrWidth(status);
    display.drawStr(statusX > 0 ? statusX : 0, 8, status);
  }

  display.drawHLine(0, 10, DISPLAY_WIDTH);
}

static void drawProgressBar(uint8_t percent)
{
  uint8_t cappedPercent = percent > 100 ? 100 : percent;
  uint8_t fillWidth = (uint8_t)(((DISPLAY_BAR_WIDTH - 2) * cappedPercent) / 100);

  display.drawFrame(DISPLAY_BAR_X, DISPLAY_BAR_Y, DISPLAY_BAR_WIDTH, DISPLAY_BAR_HEIGHT);

#if DISPLAY_FILLED_PROGRESS_BAR
  if (fillWidth > 0) {
    display.drawBox(DISPLAY_BAR_X + 1, DISPLAY_BAR_Y + 1, fillWidth, DISPLAY_BAR_HEIGHT - 2);
  }
#else
  (void)fillWidth;
#endif
}

static void showTextPage(const char *title,
                         const char *status,
                         const char *line0,
                         const char *line1,
                         const char *line2,
                         const char *line3)
{
  display.clearBuffer();
  drawHeader(title, status);
  display.setFont(u8g2_font_6x10_tf);

  if (line0 != nullptr && line0[0] != '\0') {
    display.drawStr(0, 22, line0);
  }
  if (line1 != nullptr && line1[0] != '\0') {
    display.drawStr(0, 34, line1);
  }
  if (line2 != nullptr && line2[0] != '\0') {
    display.drawStr(0, 46, line2);
  }
  if (line3 != nullptr && line3[0] != '\0') {
    display.drawStr(0, 58, line3);
  }

  display.sendBuffer();
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

static uint16_t faultFlagsFromBatteryInfo(const BmsMessage &msg)
{
  uint16_t flags = 0;

  if (msg.payload.batteryInfo.faultCellVoltageHigh) {
    flags |= 1 << 0;
  }
  if (msg.payload.batteryInfo.faultCellVoltageLow) {
    flags |= 1 << 1;
  }
  if (msg.payload.batteryInfo.faultChargeCurrentHigh) {
    flags |= 1 << 2;
  }
  if (msg.payload.batteryInfo.faultDischargeCurrentHigh) {
    flags |= 1 << 3;
  }
  if (msg.payload.batteryInfo.faultBMSTemperatureHigh) {
    flags |= 1 << 4;
  }
  if (msg.payload.batteryInfo.faultCellTemperatureHigh) {
    flags |= 1 << 5;
  }
  if (msg.payload.batteryInfo.faultCellTemperatureLow) {
    flags |= 1 << 6;
  }

  return flags;
}

static void setupPageButton()
{
  pinMode(DISPLAY_PAGE_BUTTON_PIN, INPUT_PULLUP);
  pageButtonStableState = digitalRead(DISPLAY_PAGE_BUTTON_PIN);
  attachInterrupt(digitalPinToInterrupt(DISPLAY_PAGE_BUTTON_PIN), onPageButtonInterrupt, CHANGE);
}

static void forceDisplayRefresh()
{
  latestDisplayUpdateMs = 0;
}

static void pollPageButton()
{
  if (!pageButtonInterruptSeen) {
    return;
  }

  noInterrupts();
  pageButtonInterruptSeen = false;
  interrupts();

  bool reading = digitalRead(DISPLAY_PAGE_BUTTON_PIN);
  uint32_t now = millis();

  if (now - pageButtonLastChangeMs < DISPLAY_BUTTON_DEBOUNCE_MS) {
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

static void setupDirectDisplay()
{
  display.begin();
#if DISPLAY_CONTRAST >= 0
  display.setContrast(DISPLAY_CONTRAST);
#endif
  setupPageButton();
#if BMS_DISPLAY_STANDALONE_TEST
  displayPage = 1;
#endif
  showTextPage("BMS direto", "SPI", "Display 128x64", "ST7565 pronto", "Aguardando BMS", "");
}

static void handleDirectDisplayMessage(const BmsMessage &msg)
{
  if (msg.type == BMS_TYPE_BATTERY_INFO) {
    displayState.battery = msg;
    displayState.hasBattery = true;
    displayState.batteryAtMs = millis();
  } else if (msg.type == BMS_TYPE_CELL_VOLTAGES) {
    displayState.cells = msg;
    displayState.hasCells = true;
  }
}

#if BMS_DISPLAY_STANDALONE_TEST
static BmsMessage makeDemoBatteryMessage(uint32_t sequence)
{
  BmsMessage msg = {};
  uint8_t phase = sequence % 24;

  msg.deviceId = 1;
  msg.type = BMS_TYPE_BATTERY_INFO;
  msg.payload.batteryInfo.batteryChargeEnable = phase < 16;
  msg.payload.batteryInfo.batteryChargeImmediately = phase < 4;
  msg.payload.batteryInfo.batteryDischargeEnable = phase != 20;
  msg.payload.batteryInfo.batteryTemperatureValid = phase != 11;
  msg.payload.batteryInfo.faultCellVoltageHigh = (phase == 18);
  msg.payload.batteryInfo.faultCellVoltageLow = (phase == 20);
  msg.payload.batteryInfo.faultChargeCurrentHigh = (phase == 19);
  msg.payload.batteryInfo.faultDischargeCurrentHigh = (phase == 21);
  msg.payload.batteryInfo.faultBMSTemperatureHigh = (phase == 22);
  msg.payload.batteryInfo.faultCellTemperatureHigh = (phase == 23);
  msg.payload.batteryInfo.faultCellTemperatureLow = (phase == 17);

  if (phase < 12) {
    msg.payload.batteryInfo.voltage = 52.1f + phase * 0.16f;
    msg.payload.batteryInfo.current = 6.0f + phase * 0.35f;
    msg.payload.batteryInfo.soc = 42 + phase * 3;
  } else {
    uint8_t dischargePhase = phase - 12;
    msg.payload.batteryInfo.voltage = 54.0f - dischargePhase * 0.18f;
    msg.payload.batteryInfo.current = -4.5f - dischargePhase * 0.45f;
    msg.payload.batteryInfo.soc = 78 - dischargePhase * 3;
  }

  msg.payload.batteryInfo.packPowerW =
      msg.payload.batteryInfo.voltage * msg.payload.batteryInfo.current;
  msg.payload.batteryInfo.tempC = 24.0f + (phase % 6) * 1.4f;

  return msg;
}

static BmsMessage makeDemoCellMessage(uint32_t sequence, float packVoltage)
{
  BmsMessage msg = {};
  float cellBase = packVoltage / 16.0f;
  float minV = 99.0f;
  float maxV = 0.0f;
  float sumV = 0.0f;
  float minTemp = 999.0f;
  float maxTemp = -999.0f;
  float sumTemp = 0.0f;

  msg.deviceId = 1;
  msg.type = BMS_TYPE_CELL_VOLTAGES;
  msg.payload.cellInfo.validCellCount = 16;
  msg.payload.cellInfo.validTemperatureCount = 4;
  msg.payload.cellInfo.cellsTempsRegsRead = 10;

  for (uint8_t i = 0; i < msg.payload.cellInfo.validCellCount; i++) {
    int8_t offsetIndex = (int8_t)((sequence + i) % 5) - 2;
    float cellV = cellBase + offsetIndex * 0.004f;

    if ((sequence % 24) == 20 && i == 0) {
      cellV -= 0.030f;
    }

    msg.payload.cellInfo.cellVoltages[i] = cellV;
    sumV += cellV;
    if (cellV < minV) {
      minV = cellV;
    }
    if (cellV > maxV) {
      maxV = cellV;
    }
  }

  for (uint8_t i = 0; i < msg.payload.cellInfo.validTemperatureCount; i++) {
    float cellTemp = 23.0f + ((sequence + i) % 6) * 1.5f;

    if ((sequence % 24) == 23 && i == 3) {
      cellTemp += 6.0f;
    }

    msg.payload.cellInfo.cellTemperatures[i] = cellTemp;
    sumTemp += cellTemp;
    if (cellTemp < minTemp) {
      minTemp = cellTemp;
    }
    if (cellTemp > maxTemp) {
      maxTemp = cellTemp;
    }
  }

  msg.payload.cellInfo.cellMinV = minV;
  msg.payload.cellInfo.cellMaxV = maxV;
  msg.payload.cellInfo.cellAvgV = sumV / msg.payload.cellInfo.validCellCount;
  msg.payload.cellInfo.cellSumV = sumV;
  msg.payload.cellInfo.cellDeltaMv = (maxV - minV) * 1000.0f;
  msg.payload.cellInfo.tempMinC = minTemp;
  msg.payload.cellInfo.tempMaxC = maxTemp;
  msg.payload.cellInfo.tempAvgC = sumTemp / msg.payload.cellInfo.validTemperatureCount;
  msg.payload.cellInfo.tempDeltaC = maxTemp - minTemp;

  return msg;
}

static void updateDemoBmsMessages()
{
  uint32_t now = millis();

  if (displayState.hasBattery && now - demoBmsUpdateMs < DEMO_BMS_UPDATE_INTERVAL_MS) {
    return;
  }

  BmsMessage battery = makeDemoBatteryMessage(demoBmsSequence);
  BmsMessage cells = makeDemoCellMessage(demoBmsSequence, battery.payload.batteryInfo.voltage);

  demoBmsUpdateMs = now;
  handleDirectDisplayMessage(battery);
  handleDirectDisplayMessage(cells);
  printSerialMessage(battery);
  printSerialMessage(cells);
  demoBmsSequence++;
}
#endif

static void showDirectWaitingPage()
{
  showTextPage("BMS direto", "SPI", "Sem dados da BMS", "Confira RS485", "Aguardando leitura", "");
}

static void showDirectMainPage(const BmsMessage &msg, bool stale)
{
  char socText[4];
  char powerText[5];
  char footer[24];
  char status[12];
  uint16_t socPercent = cappedSocPercent(msg.payload.batteryInfo.soc);
  char powerUnit = formatBigPower(msg.payload.batteryInfo.packPowerW, powerText, sizeof(powerText));

  snprintf(socText, sizeof(socText), "%u", socPercent);
  snprintf(footer, sizeof(footer), "PWR %s %s", powerText, powerUnitText(powerUnit));
  formatPageStatus(status, sizeof(status), 0, stale);

  display.clearBuffer();
  drawHeader("BMS direto", status);

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(0, 22, "SOC");
  display.drawStr(72, 22, "PWR");

  display.setFont(u8g2_font_logisoso18_tn);
  display.drawStr(0, 47, socText);
  display.drawStr(68, 47, powerText);

  display.setFont(u8g2_font_6x10_tf);
  display.drawStr(44, 46, "%");
  display.drawStr(110, 46, powerUnitText(powerUnit));
  drawProgressBar((uint8_t)socPercent);
  display.drawStr(0, 63, footer);
  display.sendBuffer();
}

static void showDirectElectricalPage(const BmsMessage &msg, bool stale)
{
  char line0[24];
  char line1[24];
  char line2[24];
  char line3[24];
  char status[12];
  uint16_t socPercent = cappedSocPercent(msg.payload.batteryInfo.soc);

  formatPageStatus(status, sizeof(status), 1, stale);
  snprintf(line0, sizeof(line0), "Volt  %5.2f V", msg.payload.batteryInfo.voltage);
  snprintf(line1, sizeof(line1), "Corr  %5.1f A", msg.payload.batteryInfo.current);
  snprintf(line2, sizeof(line2), "Pot   %5ld W", displayWatts(msg.payload.batteryInfo.packPowerW));
  snprintf(line3, sizeof(line3), "SOC   %3u %%", socPercent);

  showTextPage("Eletrica", status, line0, line1, line2, line3);
}

static void showDirectLifepo4Page(const BmsMessage &msg, bool stale)
{
  char line0[24];
  char line1[24];
  char line2[24];
  char line3[24];
  char status[12];
  uint8_t voltageSoc = lifepo4SocFromRestingVoltage(msg.payload.batteryInfo.voltage);
  uint16_t bmsSoc = cappedSocPercent(msg.payload.batteryInfo.soc);

  formatPageStatus(status, sizeof(status), 2, stale);
  snprintf(line0, sizeof(line0), "BMS SOC %3u %%", bmsSoc);
  snprintf(line1, sizeof(line1), "VoltSOC %3u %%", voltageSoc);
  snprintf(line2, sizeof(line2), "Pack    %5.2f V", msg.payload.batteryInfo.voltage);
  snprintf(line3, sizeof(line3), "OCV so em repouso");

  showTextPage("SOC 48V", status, line0, line1, line2, line3);
}

static void showDirectStatusPage(const BmsMessage &msg, bool stale)
{
  char line0[24];
  char line1[24];
  char line2[24];
  char line3[24];
  char status[12];

  formatPageStatus(status, sizeof(status), 3, stale);

  snprintf(line0, sizeof(line0), "BMS ID %u", msg.deviceId);
  if (msg.payload.batteryInfo.batteryTemperatureValid) {
    snprintf(line1, sizeof(line1), "Temp   %4.1f C", msg.payload.batteryInfo.tempC);
  } else {
    snprintf(line1, sizeof(line1), "Temp   invalida");
  }

  snprintf(line2, sizeof(line2), "Charge %s", onOffText(msg.payload.batteryInfo.batteryChargeEnable));
  snprintf(line3, sizeof(line3), "Dischg %s", onOffText(msg.payload.batteryInfo.batteryDischargeEnable));

  showTextPage("Status", status, line0, line1, line2, line3);
}

static void showDirectCellsFaultPage(uint32_t ageMs, bool stale)
{
  char line0[24];
  char line1[24];
  char line2[24];
  char line3[24];
  char status[12];
  uint16_t faultFlags = faultFlagsFromBatteryInfo(displayState.battery);

  formatPageStatus(status, sizeof(status), 4, stale);

  if (faultFlags != 0) {
    snprintf(line0, sizeof(line0), "Falha 0x%04X", faultFlags);
  } else {
    snprintf(line0, sizeof(line0), "Sem falhas");
  }

  if (displayState.hasCells) {
    snprintf(line1, sizeof(line1), "Cell min %4.3fV", displayState.cells.payload.cellInfo.cellMinV);
    snprintf(line2, sizeof(line2), "Cell max %4.3fV", displayState.cells.payload.cellInfo.cellMaxV);
  } else {
    snprintf(line1, sizeof(line1), "Sem dados de cel");
    line2[0] = '\0';
  }

  snprintf(line3, sizeof(line3), "Link %s %3lus",
           stale ? "OLD" : "OK ",
           (unsigned long)cappedAgeSeconds(ageMs));

  showTextPage("Falhas", status, line0, line1, line2, line3);
}

static void updateDirectDisplay()
{
  if (millis() - latestDisplayUpdateMs < DISPLAY_REFRESH_INTERVAL_MS) {
    return;
  }

  latestDisplayUpdateMs = millis();

  if (!displayState.hasBattery) {
    showDirectWaitingPage();
    return;
  }

  uint32_t ageMs = millis() - displayState.batteryAtMs;
  bool stale = ageMs > DISPLAY_BATTERY_STALE_AFTER_MS;

  switch (displayPage) {
    case 0:
      showDirectMainPage(displayState.battery, stale);
      break;

    case 1:
      showDirectElectricalPage(displayState.battery, stale);
      break;

    case 2:
      showDirectLifepo4Page(displayState.battery, stale);
      break;

    case 3:
      showDirectStatusPage(displayState.battery, stale);
      break;

    default:
      showDirectCellsFaultPage(ageMs, stale);
      break;
  }
}
#endif

FelicityBMS * bms;
QueueHandle_t bmsQueue;
static uint32_t latestLocationPublishMs = 0;
static uint32_t latestCentralDemoEspNowMs = 0;
static uint32_t centralDemoSequence = 0;

static void publishLocation()
{
#if BMS_LOCATION_ENABLE && BMS_MQTT_ENABLE
  uint32_t now = millis();
  if (latestLocationPublishMs != 0 &&
      now - latestLocationPublishMs < BMS_LOCATION_PUBLISH_INTERVAL_MS) {
    return;
  }

  latestLocationPublishMs = now;
  char payload[320];
  if (!bmsLocationBuildJson(payload, sizeof(payload))) {
    writeLog("[GPS] Location payload truncated\n");
    return;
  }

  bmsMqttPublisherPublishRaw("location/v1", payload, true);
  writeLog("[GPS] %s\n", payload);
#endif
}

static BmsMessage makeCentralDemoBatteryMessage()
{
  float phase = (centralDemoSequence % 60) / 60.0f;
  float current = -10.0f + sinf(phase * 6.2831853f) * 4.0f;
  float voltage = 52.2f + sinf(phase * 6.2831853f * 0.7f) * 0.5f;

  BmsMessage msg = {};
  msg.deviceId = 1;
  msg.type = BMS_TYPE_BATTERY_INFO;
  msg.payload.batteryInfo.batteryChargeEnable = true;
  msg.payload.batteryInfo.batteryDischargeEnable = true;
  msg.payload.batteryInfo.voltage = voltage;
  msg.payload.batteryInfo.current = current;
  msg.payload.batteryInfo.packPowerW = voltage * current;
  msg.payload.batteryInfo.soc = 72 + (uint16_t)(sinf(phase * 6.2831853f * 0.4f) * 5.0f);
  msg.payload.batteryInfo.batteryTemperatureValid = true;
  msg.payload.batteryInfo.tempC = 29.0f + sinf(phase * 6.2831853f) * 2.0f;
  return msg;
}

static void updateCentralDemoEspNow()
{
#if BMS_CENTRAL_DEMO_ESPNOW
  uint32_t now = millis();
  if (latestCentralDemoEspNowMs != 0 &&
      now - latestCentralDemoEspNowMs < BMS_CENTRAL_DEMO_ESPNOW_INTERVAL_MS) {
    return;
  }

  latestCentralDemoEspNowMs = now;
  centralDemoSequence++;
  BmsMessage msg = makeCentralDemoBatteryMessage();
  espnowBatteryHandleMessage(msg);
  printSerialMessage(msg);
#endif
}

static void startBmsTasks(int rx, int tx, int de, int re, int battery_count)
{
  Serial.printf("RS485 RX/RO = GPIO%d\n", rx);
  Serial.printf("RS485 TX/DI = GPIO%d\n", tx);
  Serial.printf("RS485 DE    = GPIO%d\n", de);
  Serial.printf("RS485 RE    = GPIO%d\n", re);
  Serial.printf("BMS slaves  = %d\n", battery_count);

  bms = new FelicityBMS(rx, tx, de, re, battery_count);
  bmsQueue = xQueueCreate(10, sizeof(BmsMessage));
  bms->SetQueue(bmsQueue);

  xTaskCreatePinnedToCore(FelicityBMS::bmsTaskWrapper, "BMS", 4096, bms, 1, NULL, FELICITY_TASK_CORE);
}

static void printSerialMessage(const BmsMessage &msg)
{
  Serial.printf("[BMS%d]\n", msg.deviceId);

  switch (msg.type) {
    case BMS_TYPE_VERSION_INFO:
      Serial.printf("version = %u\n", (unsigned)msg.payload.versionInfo.version);
      break;

    case BMS_TYPE_CELL_VOLTAGES:
      Serial.printf("cells_temps_regs_read = %u\n", (unsigned)msg.payload.cellInfo.cellsTempsRegsRead);
      for (uint8_t i = 0; i < msg.payload.cellInfo.validCellCount; i++) {
        Serial.printf("cell_voltage_%u = %.3f\n", (unsigned)i, msg.payload.cellInfo.cellVoltages[i]);
      }
      Serial.printf("valid_cell_count = %u\n", (unsigned)msg.payload.cellInfo.validCellCount);
      Serial.printf("cell_min_v = %.3f\n", msg.payload.cellInfo.cellMinV);
      Serial.printf("cell_max_v = %.3f\n", msg.payload.cellInfo.cellMaxV);
      Serial.printf("cell_avg_v = %.3f\n", msg.payload.cellInfo.cellAvgV);
      Serial.printf("cell_sum_v = %.3f\n", msg.payload.cellInfo.cellSumV);
      Serial.printf("cell_delta_mv = %.0f\n", msg.payload.cellInfo.cellDeltaMv);

      for (uint8_t i = 0; i < msg.payload.cellInfo.validTemperatureCount; i++) {
        Serial.printf("cell_temperature_%u = %.0f\n", (unsigned)i, msg.payload.cellInfo.cellTemperatures[i]);
      }
      Serial.printf("valid_temperature_count = %u\n", (unsigned)msg.payload.cellInfo.validTemperatureCount);
      Serial.printf("temp_min_c = %.1f\n", msg.payload.cellInfo.tempMinC);
      Serial.printf("temp_max_c = %.1f\n", msg.payload.cellInfo.tempMaxC);
      Serial.printf("temp_avg_c = %.1f\n", msg.payload.cellInfo.tempAvgC);
      Serial.printf("temp_delta_c = %.1f\n", msg.payload.cellInfo.tempDeltaC);
      break;

    case BMS_TYPE_CHARGE_DISCHARGE:
      Serial.printf("charge_current_limit = %.1f\n", msg.payload.chargeDischarge.chargeCurrentLimit);
      Serial.printf("charge_voltage_limit = %.2f\n", msg.payload.chargeDischarge.chargeVoltLimit);
      Serial.printf("discharge_current_limit = %.1f\n", msg.payload.chargeDischarge.dischargeCurrentLimit);
      Serial.printf("discharge_voltage_limit = %.2f\n", msg.payload.chargeDischarge.dischargeVoltLimit);
      break;

    case BMS_TYPE_BATTERY_INFO:
      Serial.printf("voltage = %.2f\n", msg.payload.batteryInfo.voltage);
      Serial.printf("current = %.2f\n", msg.payload.batteryInfo.current);
      Serial.printf("pack_power_w = %.1f\n", msg.payload.batteryInfo.packPowerW);
      Serial.printf("soc = %u\n", (unsigned)msg.payload.batteryInfo.soc);
      if (msg.payload.batteryInfo.batteryTemperatureValid) {
        Serial.printf("temperature = %.1f\n", msg.payload.batteryInfo.tempC);
      } else {
        Serial.println("temperature = INVALID");
      }
      Serial.printf("battery_charge_enable = %s\n", msg.payload.batteryInfo.batteryChargeEnable ? "true" : "false");
      Serial.printf("battery_charge_immediately = %s\n", msg.payload.batteryInfo.batteryChargeImmediately ? "true" : "false");
      Serial.printf("battery_discharge_enable = %s\n", msg.payload.batteryInfo.batteryDischargeEnable ? "true" : "false");
      Serial.printf("fault_bms_temperature_high = %s\n", msg.payload.batteryInfo.faultBMSTemperatureHigh ? "true" : "false");
      Serial.printf("fault_cell_temperature_high = %s\n", msg.payload.batteryInfo.faultCellTemperatureHigh ? "true" : "false");
      Serial.printf("fault_cell_temperature_low = %s\n", msg.payload.batteryInfo.faultCellTemperatureLow ? "true" : "false");
      Serial.printf("fault_cell_voltage_high = %s\n", msg.payload.batteryInfo.faultCellVoltageHigh ? "true" : "false");
      Serial.printf("fault_cell_voltage_low = %s\n", msg.payload.batteryInfo.faultCellVoltageLow ? "true" : "false");
      Serial.printf("fault_charge_current_high = %s\n", msg.payload.batteryInfo.faultChargeCurrentHigh ? "true" : "false");
      Serial.printf("fault_discharge_current_high = %s\n", msg.payload.batteryInfo.faultDischargeCurrentHigh ? "true" : "false");
      break;

    default:
      Serial.println("unknown_message_type = true");
      break;
  }

  Serial.println();
}

#if !BMS_LCD_DIRECT_MODE
static void serial_debug_task(void *param)
{
  (void)param;

  BmsMessage msg;
  for (;;) {
    if (xQueueReceive(bmsQueue, &msg, portMAX_DELAY) == pdTRUE) {
      bmsSdLoggerHandleMessage(msg);
      bmsLoraPublisherHandleMessage(msg);
      espnowBatteryHandleMessage(msg);
      printSerialMessage(msg);
    }
  }
}
#endif

void setup()
{
  Serial.begin(SERIAL_DEBUG_BAUD);
  Serial.println("Starting up...");

#if BMS_CENTRAL_MODE
  Serial.println("Gateway mode: ESP32-S3 central, RS485 input, MQTT/LoRa/front-panel output.");
  Serial.println("Local microSD logging is disabled in this mode.");

#if BMS_CENTRAL_DEMO_ESPNOW
  Serial.println("Central ESP-NOW demo mode: fake BMS packets only.");
  espnowBatteryBegin();
#else
  bmsMqttPublisherBegin();
  bmsOtaBegin();
  espnowBatteryBegin();
  bmsLoraPublisherBegin();
  bmsLocationBegin();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
#endif
#elif BMS_LCD_DIRECT_MODE
  Serial.println("Gateway mode: RS485 input, direct display output.");
  Serial.println("ESP-NOW is disabled in this firmware.");

  setupDirectDisplay();
#if DISPLAY_CONTRAST >= 0
  Serial.printf("Display contrast = %d\n", DISPLAY_CONTRAST);
#endif
#if DISPLAY_USE_ERC12864_ALT
  Serial.println("Display driver = ERC12864_ALT");
#else
  Serial.println("Display driver = ERC12864");
#endif
#if BMS_DISPLAY_STANDALONE_TEST
  Serial.println("Standalone display test: simulating BMS data.");
  showTextPage("Teste BMS", "SPI", "Simulando bateria", "Sem RS485 real", "Botao troca pag", "");
#else
  bmsSdLoggerBegin();
  bmsLoraPublisherBegin();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
  bmsMqttPublisherBegin();
#endif
#else
  Serial.println("Gateway mode: RS485 input, ESP-NOW output.");
  Serial.println("MQTT/WiFi publishing is disabled in this firmware.");

  espnowBatteryBegin();
  bmsSdLoggerBegin();
  bmsLoraPublisherBegin();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
  xTaskCreatePinnedToCore(serial_debug_task, "SerialDebug", 4096, NULL, 1, NULL, FELICITY_TASK_CORE);
#endif

  Serial.println("System started.");
}

void loop()
{
#if BMS_CENTRAL_MODE
#if BMS_CENTRAL_DEMO_ESPNOW
  updateCentralDemoEspNow();
  delay(10);
#else
  BmsMessage msg;
  while (bmsQueue != nullptr && xQueueReceive(bmsQueue, &msg, 0) == pdTRUE) {
    espnowBatteryHandleMessage(msg);
    bmsMqttPublisherHandleMessage(msg);
    bmsLoraPublisherHandleMessage(msg);
    printSerialMessage(msg);
  }

  bmsOtaLoop();
  bmsLocationLoop();
  publishLocation();
  bmsMqttPublisherLoop();
  bmsLoraPublisherLoop();
  delay(10);
#endif
#elif BMS_LCD_DIRECT_MODE
  pollPageButton();

#if BMS_DISPLAY_STANDALONE_TEST
  updateDemoBmsMessages();
  updateDirectDisplay();
  delay(10);
#else
  BmsMessage msg;
  while (bmsQueue != nullptr && xQueueReceive(bmsQueue, &msg, 0) == pdTRUE) {
    handleDirectDisplayMessage(msg);
    bmsSdLoggerHandleMessage(msg);
    bmsMqttPublisherHandleMessage(msg);
    bmsLoraPublisherHandleMessage(msg);
    printSerialMessage(msg);
  }

  updateDirectDisplay();
  bmsMqttPublisherLoop();
  bmsLoraPublisherLoop();
  delay(10);
#endif
#else
  delay(1000);
#endif
}
