//
// Solmar BMS ESP-NOW gateway
//
#include <Arduino.h>

#include "espnow_battery.h"
#include "felicity.h"
#include "main.h"

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
      Serial.printf("temperature = %u\n", (unsigned)msg.payload.batteryInfo.temp);
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

void setup()
{
  Serial.begin(SERIAL_DEBUG_BAUD);
  Serial.println("Starting up...");
  Serial.println("Gateway mode: RS485 input, ESP-NOW output.");
  Serial.println("MQTT/WiFi publishing is disabled in this firmware.");

  espnowBatteryBegin();
  startBmsTasks(RS485_RX_PIN, RS485_TX_PIN, RS485_DE_PIN, RS485_RE_PIN, BMS_BATTERY_COUNT);
  xTaskCreatePinnedToCore(serial_debug_task, "SerialDebug", 4096, NULL, 1, NULL, FELICITY_TASK_CORE);

  Serial.println("System started.");
}

void loop()
{
  delay(1000);
}
