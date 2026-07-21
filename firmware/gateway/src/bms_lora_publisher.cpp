#include "bms_lora_publisher.h"

#ifndef BMS_LORA_ENABLE
#define BMS_LORA_ENABLE 0
#endif

#if BMS_LORA_ENABLE

#include <Arduino.h>
#include <string.h>

#include "bms_telemetry_json.h"
#include "main.h"

#define BMS_LORA_BACKEND_E90_DTU 1
#define BMS_LORA_BACKEND_E220 2

#ifndef BMS_LORA_BACKEND
#define BMS_LORA_BACKEND BMS_LORA_BACKEND_E220
#endif

#ifndef BMS_LORA_UART_NUM
#define BMS_LORA_UART_NUM 2
#endif

#ifndef BMS_LORA_BAUD
#define BMS_LORA_BAUD 9600
#endif

#ifndef BMS_LORA_RX_PIN
#define BMS_LORA_RX_PIN 14
#endif

#ifndef BMS_LORA_TX_PIN
#define BMS_LORA_TX_PIN 13
#endif

#ifndef BMS_LORA_E90_DE_RE_PIN
#define BMS_LORA_E90_DE_RE_PIN -1
#endif

#ifndef BMS_LORA_E220_M0_PIN
#define BMS_LORA_E220_M0_PIN -1
#endif

#ifndef BMS_LORA_E220_M1_PIN
#define BMS_LORA_E220_M1_PIN -1
#endif

#ifndef BMS_LORA_E220_AUX_PIN
#define BMS_LORA_E220_AUX_PIN -1
#endif

#ifndef BMS_LORA_BUFFER_SIZE
#define BMS_LORA_BUFFER_SIZE 1536
#endif

#ifndef BMS_LORA_MIN_INTERVAL_MS
#define BMS_LORA_MIN_INTERVAL_MS 5000
#endif

#ifndef BMS_LORA_BATTERY_INFO_ONLY
#define BMS_LORA_BATTERY_INFO_ONLY 1
#endif

#ifndef BMS_LORA_E90_TURNAROUND_DELAY_MS
#define BMS_LORA_E90_TURNAROUND_DELAY_MS 2
#endif

#ifndef BMS_LORA_AUX_TIMEOUT_MS
#define BMS_LORA_AUX_TIMEOUT_MS 1000
#endif

#ifndef BMS_LORA_RX_DEBUG
#define BMS_LORA_RX_DEBUG 0
#endif

#if !defined(ARDUINO_ARCH_ESP32)
#error "BMS_LORA_ENABLE currently expects an ESP32 HardwareSerial backend."
#endif

static HardwareSerial loraSerial(BMS_LORA_UART_NUM);
static bool loraStarted = false;
static bool loraReady = false;
static uint32_t loraSequence = 0;
static uint32_t latestLoraSendMs = 0;
static char loraPayloadBuffer[BMS_LORA_BUFFER_SIZE];

static bool isConfiguredPin(int pin)
{
  return pin >= 0;
}

static const char *loraBackendName()
{
#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E90_DTU
  return "E90-DTU";
#elif BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  return "E220-900T22D";
#else
  return "unknown";
#endif
}

static void configureBackendPins()
{
#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E90_DTU
  if (isConfiguredPin(BMS_LORA_E90_DE_RE_PIN)) {
    pinMode(BMS_LORA_E90_DE_RE_PIN, OUTPUT);
    digitalWrite(BMS_LORA_E90_DE_RE_PIN, LOW);
  }
#elif BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  if (isConfiguredPin(BMS_LORA_E220_M0_PIN)) {
    pinMode(BMS_LORA_E220_M0_PIN, OUTPUT);
    digitalWrite(BMS_LORA_E220_M0_PIN, LOW);
  }

  if (isConfiguredPin(BMS_LORA_E220_M1_PIN)) {
    pinMode(BMS_LORA_E220_M1_PIN, OUTPUT);
    digitalWrite(BMS_LORA_E220_M1_PIN, LOW);
  }

  if (isConfiguredPin(BMS_LORA_E220_AUX_PIN)) {
    pinMode(BMS_LORA_E220_AUX_PIN, INPUT);
  }

  delay(50);
#endif
}

static bool waitForE220Aux(uint32_t timeoutMs)
{
#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  if (!isConfiguredPin(BMS_LORA_E220_AUX_PIN)) {
    return true;
  }

  uint32_t startedMs = millis();
  while (digitalRead(BMS_LORA_E220_AUX_PIN) == LOW) {
    if (millis() - startedMs >= timeoutMs) {
      return false;
    }
    delay(1);
  }
#else
  (void)timeoutMs;
#endif

  return true;
}

static void beginTransmit()
{
#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E90_DTU
  if (isConfiguredPin(BMS_LORA_E90_DE_RE_PIN)) {
    digitalWrite(BMS_LORA_E90_DE_RE_PIN, HIGH);
    delay(BMS_LORA_E90_TURNAROUND_DELAY_MS);
  }
#elif BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  if (!waitForE220Aux(BMS_LORA_AUX_TIMEOUT_MS)) {
    writeLog("[LORA] AUX timeout before transmit\n");
  }
#endif
}

static void endTransmit()
{
  loraSerial.flush();

#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E90_DTU
  if (isConfiguredPin(BMS_LORA_E90_DE_RE_PIN)) {
    delay(BMS_LORA_E90_TURNAROUND_DELAY_MS);
    digitalWrite(BMS_LORA_E90_DE_RE_PIN, LOW);
  }
#elif BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  if (!waitForE220Aux(BMS_LORA_AUX_TIMEOUT_MS)) {
    writeLog("[LORA] AUX timeout after transmit\n");
  }
#endif
}

static bool shouldSendMessage(const BmsMessage &msg, uint32_t nowMs)
{
#if BMS_LORA_BATTERY_INFO_ONLY
  if (msg.type != BMS_TYPE_BATTERY_INFO) {
    return false;
  }
#else
  (void)msg;
#endif

#if BMS_LORA_MIN_INTERVAL_MS > 0
  if (latestLoraSendMs != 0 && nowMs - latestLoraSendMs < BMS_LORA_MIN_INTERVAL_MS) {
    return false;
  }
#else
  (void)nowMs;
#endif

  return true;
}

static bool writeLoraJsonLine(const BmsMessage &msg, uint32_t nowMs)
{
  if (!bmsTelemetryBuildJson(msg, ++loraSequence, nowMs, loraPayloadBuffer, sizeof(loraPayloadBuffer))) {
    writeLog("[LORA] JSON payload truncated for BMS%u %s\n",
             (unsigned)msg.deviceId,
             bmsTelemetryMessageTypeName(msg.type));
    return false;
  }

  size_t payloadLength = strlen(loraPayloadBuffer);
  if (payloadLength == 0) {
    writeLog("[LORA] Empty payload for BMS%u %s\n",
             (unsigned)msg.deviceId,
             bmsTelemetryMessageTypeName(msg.type));
    return false;
  }

  beginTransmit();
  size_t written = loraSerial.write((const uint8_t *)loraPayloadBuffer, payloadLength);
  written += loraSerial.write('\n');
  endTransmit();

  if (written != payloadLength + 1) {
    writeLog("[LORA] UART write failed, written=%u expected=%u\n",
             (unsigned)written,
             (unsigned)(payloadLength + 1));
    return false;
  }

  latestLoraSendMs = nowMs;
  return true;
}

bool bmsLoraPublisherBegin()
{
  if (loraStarted) {
    return loraReady;
  }

  loraStarted = true;
  configureBackendPins();
  loraSerial.begin(BMS_LORA_BAUD, SERIAL_8N1, BMS_LORA_RX_PIN, BMS_LORA_TX_PIN);
  loraReady = true;

  writeLog("[LORA] Backend: %s\n", loraBackendName());
  writeLog("[LORA] UART%d baud=%u RX=GPIO%d TX=GPIO%d\n",
           BMS_LORA_UART_NUM,
           (unsigned)BMS_LORA_BAUD,
           BMS_LORA_RX_PIN,
           BMS_LORA_TX_PIN);

#if BMS_LORA_BACKEND == BMS_LORA_BACKEND_E90_DTU
  if (isConfiguredPin(BMS_LORA_E90_DE_RE_PIN)) {
    writeLog("[LORA] E90-DTU RS485 DE/RE=GPIO%d\n", BMS_LORA_E90_DE_RE_PIN);
  } else {
    writeLog("[LORA] E90-DTU serial mode without local DE/RE control\n");
  }
#elif BMS_LORA_BACKEND == BMS_LORA_BACKEND_E220
  writeLog("[LORA] E220 normal mode M0=GPIO%d M1=GPIO%d AUX=GPIO%d\n",
           BMS_LORA_E220_M0_PIN,
           BMS_LORA_E220_M1_PIN,
           BMS_LORA_E220_AUX_PIN);
#endif

  return loraReady;
}

void bmsLoraPublisherLoop()
{
#if BMS_LORA_RX_DEBUG
  if (!loraStarted) {
    return;
  }

  while (loraSerial.available() > 0) {
    Serial.write((uint8_t)loraSerial.read());
  }
#endif
}

bool bmsLoraPublisherHandleMessage(const BmsMessage &msg)
{
  if (!bmsLoraPublisherBegin()) {
    return false;
  }

  uint32_t nowMs = millis();
  if (!shouldSendMessage(msg, nowMs)) {
    return false;
  }

  return writeLoraJsonLine(msg, nowMs);
}

bool bmsLoraPublisherIsReady()
{
  return loraReady;
}

#else

bool bmsLoraPublisherBegin()
{
  return false;
}

void bmsLoraPublisherLoop()
{
}

bool bmsLoraPublisherHandleMessage(const BmsMessage &msg)
{
  (void)msg;
  return false;
}

bool bmsLoraPublisherIsReady()
{
  return false;
}

#endif
