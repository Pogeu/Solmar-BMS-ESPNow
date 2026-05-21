//
// This is part of the FelicityBMS2MQTT project
//
// https://github.com/Smartsmurf/FelicityBMS2MQTT
// 
// 
#ifndef FELICITY_MAIN_H
#define FELICITY_MAIN_H

#include <Arduino.h>

#ifndef SERIAL_ONLY_MODE
#define SERIAL_ONLY_MODE 0
#endif

#ifndef SERIAL_DEBUG_BAUD
#define SERIAL_DEBUG_BAUD 9600
#endif

#ifndef BMS_BAUD_RATE
#define BMS_BAUD_RATE 9600
#endif

#ifndef BMS_BATTERY_COUNT
#define BMS_BATTERY_COUNT 1
#endif

#ifndef FELICITY_VERBOSE_MODBUS
#define FELICITY_VERBOSE_MODBUS 0
#endif

#ifndef LED_PIN
#define LED_PIN 2
#endif

#define writeLog(...) Serial.printf(__VA_ARGS__)

// Default wiring is for an ESP32-C3 with RO->GPIO0, RE/DE->GPIO1, DI->GPIO2.
#ifndef RS485_RX_PIN
#define RS485_RX_PIN 0
#endif

#ifndef RS485_TX_PIN
#define RS485_TX_PIN 2
#endif

#ifndef RS485_DE_RE_PIN
#define RS485_DE_RE_PIN 1
#endif

#ifndef RS485_DE_PIN
#define RS485_DE_PIN RS485_DE_RE_PIN
#endif

#ifndef RS485_RE_PIN
#define RS485_RE_PIN RS485_DE_RE_PIN
#endif

#if (defined(CONFIG_FREERTOS_UNICORE) && CONFIG_FREERTOS_UNICORE) || defined(CONFIG_IDF_TARGET_ESP32C3)
#define FELICITY_TASK_CORE 0
#else
#define FELICITY_TASK_CORE 1
#endif

inline void rs485TxEnable() {
    digitalWrite(RS485_DE_PIN, HIGH);
    digitalWrite(RS485_RE_PIN, HIGH);
}

inline void rs485RxEnable() {
    digitalWrite(RS485_DE_PIN, LOW);
    digitalWrite(RS485_RE_PIN, LOW);
}

extern QueueHandle_t bmsQueue;

#endif
