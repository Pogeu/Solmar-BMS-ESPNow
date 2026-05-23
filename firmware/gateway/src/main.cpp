//
// Solmar BMS ESP-NOW gateway
//
#include <Arduino.h>

#if BMS_LCD_DIRECT_MODE
#include <Wire.h>

#define USE_SERIAL_1602_LCD
#include <LCDBigNumbers.hpp>
#else
#include "espnow_battery.h"
#endif

#include "felicity.h"
#include "main.h"

#if BMS_LCD_DIRECT_MODE
#ifndef LCD_I2C_ADDR
#define LCD_I2C_ADDR 0x27
#endif

#ifndef LCD_COLUMNS
#define LCD_COLUMNS 16
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

constexpr uint32_t LCD_REFRESH_INTERVAL_MS = 500;
constexpr uint32_t LCD_BATTERY_STALE_AFTER_MS = 10000;
constexpr uint8_t LCD_PAGE_COUNT = 5;

struct VoltageSocPoint {
  float voltage;
  uint8_t socPercent;
};

struct DirectLcdState {
  BmsMessage battery = {};
  BmsMessage cells = {};
  bool hasBattery = false;
  bool hasCells = false;
  uint32_t batteryAtMs = 0;
};

LiquidCrystal_I2C lcd(LCD_I2C_ADDR, LCD_COLUMNS, LCD_ROWS);
LCDBigNumbers bigNumberLCD(&lcd, BIG_NUMBERS_FONT_2_COLUMN_2_ROWS_VARIANT_1);

DirectLcdState lcdState;
char displayedLines[LCD_ROWS][LCD_COLUMNS + 1] = {};
char displayedMainSoc[4] = {};
char displayedMainPower[5] = {};
char displayedMainPowerUnit = '\0';
bool displayedMainStale = false;
bool mainPageRendered = false;
uint32_t latestLcdUpdateMs = 0;
uint8_t lcdPage = 0;

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

static void forceLcdRefresh()
{
  latestLcdUpdateMs = 0;
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
    lcdPage = (lcdPage + 1) % LCD_PAGE_COUNT;
    forceLcdRefresh();
  }
}

static void setupDirectLcd()
{
  setupDisplayBus();
  setupPageButton();
  lcd.init();
  lcd.backlight();
  bigNumberLCD.begin();
  bigNumberLCD.disableGapBetweenNumbers();
  lcdPrintLine(0, "BMS LCD direto");
  lcdPrintLine(1, "Aguardando BMS");
}

static void handleDirectLcdMessage(const BmsMessage &msg)
{
  if (msg.type == BMS_TYPE_BATTERY_INFO) {
    lcdState.battery = msg;
    lcdState.hasBattery = true;
    lcdState.batteryAtMs = millis();
  } else if (msg.type == BMS_TYPE_CELL_VOLTAGES) {
    lcdState.cells = msg;
    lcdState.hasCells = true;
  }
}

static void showDirectWaitingPage()
{
  lcdPrintLine(0, "BMS LCD direto");
  lcdPrintLine(1, "Sem dados");
}

static void showDirectMainPage(const BmsMessage &msg, bool stale)
{
  char socText[4];
  char powerText[5];
  uint16_t socPercent = cappedSocPercent(msg.payload.batteryInfo.soc);
  char powerUnit = formatBigPower(msg.payload.batteryInfo.packPowerW, powerText, sizeof(powerText));

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

static void showDirectElectricalPage(const BmsMessage &msg)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];
  uint16_t socPercent = cappedSocPercent(msg.payload.batteryInfo.soc);

  snprintf(line0, sizeof(line0), "V%5.2f I%5.1f",
           msg.payload.batteryInfo.voltage,
           msg.payload.batteryInfo.current);
  snprintf(line1, sizeof(line1), "P%6ldW S%3u%%",
           displayWatts(msg.payload.batteryInfo.packPowerW),
           socPercent);

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showDirectLifepo4Page(const BmsMessage &msg)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];
  uint8_t voltageSoc = lifepo4SocFromRestingVoltage(msg.payload.batteryInfo.voltage);
  uint16_t bmsSoc = cappedSocPercent(msg.payload.batteryInfo.soc);

  snprintf(line0, sizeof(line0), "BMS%3u%% Volt%3u%%", bmsSoc, voltageSoc);
  snprintf(line1, sizeof(line1), "48V OCV %5.2f", msg.payload.batteryInfo.voltage);

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showDirectStatusPage(const BmsMessage &msg)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];

  if (msg.payload.batteryInfo.batteryTemperatureValid) {
    snprintf(line0, sizeof(line0), "B%u Temp %4.1fC",
             msg.deviceId,
             msg.payload.batteryInfo.tempC);
  } else {
    snprintf(line0, sizeof(line0), "B%u Temp inval", msg.deviceId);
  }

  snprintf(line1, sizeof(line1), "C:%-3s D:%-3s",
           onOffText(msg.payload.batteryInfo.batteryChargeEnable),
           onOffText(msg.payload.batteryInfo.batteryDischargeEnable));

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void showDirectCellsFaultPage(uint32_t ageMs, bool stale)
{
  char line0[LCD_COLUMNS + 1];
  char line1[LCD_COLUMNS + 1];
  uint16_t faultFlags = faultFlagsFromBatteryInfo(lcdState.battery);

  if (faultFlags != 0) {
    snprintf(line0, sizeof(line0), "FALHA 0x%04X", faultFlags);
  } else if (lcdState.hasCells) {
    snprintf(line0, sizeof(line0), "Cel %4.3f-%4.3f",
             lcdState.cells.payload.cellInfo.cellMinV,
             lcdState.cells.payload.cellInfo.cellMaxV);
  } else {
    snprintf(line0, sizeof(line0), "Sem falhas");
  }

  snprintf(line1, sizeof(line1), "Link %s %3lus",
           stale ? "OLD" : "OK ",
           (unsigned long)cappedAgeSeconds(ageMs));

  lcdPrintLine(0, line0);
  lcdPrintLine(1, line1);
}

static void updateDirectLcd()
{
  if (millis() - latestLcdUpdateMs < LCD_REFRESH_INTERVAL_MS) {
    return;
  }

  latestLcdUpdateMs = millis();

  if (!lcdState.hasBattery) {
    showDirectWaitingPage();
    return;
  }

  uint32_t ageMs = millis() - lcdState.batteryAtMs;
  bool stale = ageMs > LCD_BATTERY_STALE_AFTER_MS;

  switch (lcdPage) {
    case 0:
      showDirectMainPage(lcdState.battery, stale);
      break;

    case 1:
      showDirectElectricalPage(lcdState.battery);
      break;

    case 2:
      showDirectLifepo4Page(lcdState.battery);
      break;

    case 3:
      showDirectStatusPage(lcdState.battery);
      break;

    default:
      showDirectCellsFaultPage(ageMs, stale);
      break;
  }
}
#endif

FelicityBMS * bms;
QueueHandle_t bmsQueue;

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

#if BMS_LCD_DIRECT_MODE
  Serial.println("Gateway mode: RS485 input, direct LCD output.");
  Serial.println("ESP-NOW is disabled in this firmware.");

  setupDirectLcd();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
#else
  Serial.println("Gateway mode: RS485 input, ESP-NOW output.");
  Serial.println("MQTT/WiFi publishing is disabled in this firmware.");

  espnowBatteryBegin();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
  xTaskCreatePinnedToCore(serial_debug_task, "SerialDebug", 4096, NULL, 1, NULL, FELICITY_TASK_CORE);
#endif

  Serial.println("System started.");
}

void loop()
{
#if BMS_LCD_DIRECT_MODE
  pollPageButton();

  BmsMessage msg;
  while (bmsQueue != nullptr && xQueueReceive(bmsQueue, &msg, 0) == pdTRUE) {
    handleDirectLcdMessage(msg);
    printSerialMessage(msg);
  }

  updateDirectLcd();
  delay(10);
#else
  delay(1000);
#endif
}
