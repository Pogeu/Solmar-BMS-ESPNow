#include <Arduino.h>

#if defined(ARDUINO_ARCH_ESP32) || defined(ESP32)
#define LORA_ON_ESP32 1
#endif

#ifndef LORA_RX_PIN
#define LORA_RX_PIN 16
#ifndef LORA_ON_ESP32
#undef LORA_RX_PIN
#define LORA_RX_PIN 10
#endif
#endif

#ifndef LORA_TX_PIN
#define LORA_TX_PIN 17
#ifndef LORA_ON_ESP32
#undef LORA_TX_PIN
#define LORA_TX_PIN 11
#endif
#endif

#ifndef LORA_E220_M0_PIN
#define LORA_E220_M0_PIN 4
#ifndef LORA_ON_ESP32
#undef LORA_E220_M0_PIN
#define LORA_E220_M0_PIN 7
#endif
#endif

#ifndef LORA_E220_M1_PIN
#define LORA_E220_M1_PIN 5
#ifndef LORA_ON_ESP32
#undef LORA_E220_M1_PIN
#define LORA_E220_M1_PIN 6
#endif
#endif

#ifndef LORA_E220_AUX_PIN
#define LORA_E220_AUX_PIN 18
#ifndef LORA_ON_ESP32
#undef LORA_E220_AUX_PIN
#define LORA_E220_AUX_PIN 5
#endif
#endif

#ifdef LORA_ON_ESP32
#ifndef LORA_UART_NUM
#define LORA_UART_NUM 2
#endif
HardwareSerial loraSerial(LORA_UART_NUM);
#else
#include <SoftwareSerial.h>
SoftwareSerial loraSerial(LORA_RX_PIN, LORA_TX_PIN);
#endif

#ifndef USB_SERIAL_BAUD
#define USB_SERIAL_BAUD 115200
#endif

#ifndef LORA_BAUD
#define LORA_BAUD 9600
#endif

#ifndef LORA_AUTO_SEND_MS
#define LORA_AUTO_SEND_MS 3000
#endif

#ifndef LORA_TEST_SENDER
#define LORA_TEST_SENDER 1
#ifndef LORA_ON_ESP32
#undef LORA_TEST_SENDER
#define LORA_TEST_SENDER 0
#endif
#endif

#ifndef LORA_ALLOW_MANUAL_TX
#define LORA_ALLOW_MANUAL_TX LORA_TEST_SENDER
#endif

#ifndef LORA_AUX_TIMEOUT_MS
#define LORA_AUX_TIMEOUT_MS 1000
#endif

#ifndef LORA_RX_IDLE_LOG_MS
#define LORA_RX_IDLE_LOG_MS 10000
#endif

#ifndef LORA_NODE_NAME
#define LORA_NODE_NAME "ESP"
#ifndef LORA_ON_ESP32
#undef LORA_NODE_NAME
#define LORA_NODE_NAME "ARDUINO"
#endif
#endif

static char usbLine[96];
static size_t usbLineLength = 0;
static char rxLine[128];
static size_t rxLineLength = 0;
static unsigned long latestRxLineMs = 0;
#if LORA_TEST_SENDER && LORA_AUTO_SEND_MS > 0
static unsigned long txSequence = 0;
static unsigned long nextAutoSendMs = 0;
#endif

static bool configuredPin(int pin)
{
  return pin >= 0;
}

static void beginLoraSerial()
{
#ifdef LORA_ON_ESP32
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
#else
  loraSerial.begin(LORA_BAUD);
#endif
}

static bool waitAuxReady(unsigned long timeoutMs)
{
  if (!configuredPin(LORA_E220_AUX_PIN)) {
    return true;
  }

  unsigned long startedMs = millis();
  while (digitalRead(LORA_E220_AUX_PIN) == LOW) {
    if (millis() - startedMs >= timeoutMs) {
      return false;
    }
    delay(1);
  }

  return true;
}

static void setupE220NormalMode()
{
  if (configuredPin(LORA_E220_M0_PIN)) {
    pinMode(LORA_E220_M0_PIN, OUTPUT);
    digitalWrite(LORA_E220_M0_PIN, LOW);
  }

  if (configuredPin(LORA_E220_M1_PIN)) {
    pinMode(LORA_E220_M1_PIN, OUTPUT);
    digitalWrite(LORA_E220_M1_PIN, LOW);
  }

  if (configuredPin(LORA_E220_AUX_PIN)) {
    pinMode(LORA_E220_AUX_PIN, INPUT);
  }

  delay(50);
  waitAuxReady(LORA_AUX_TIMEOUT_MS);
}

void sendLine(const char *line)
{
  if (!waitAuxReady(LORA_AUX_TIMEOUT_MS)) {
    Serial.println(F("WARN: AUX timeout before TX"));
  }

  loraSerial.print(line);
  loraSerial.print('\n');
  loraSerial.flush();

  if (!waitAuxReady(LORA_AUX_TIMEOUT_MS)) {
    Serial.println(F("WARN: AUX timeout after TX"));
  }

  Serial.print(F("TX> "));
  Serial.println(line);
}

#if LORA_TEST_SENDER && LORA_AUTO_SEND_MS > 0
void sendPing()
{
  char line[96];
  snprintf(line, sizeof(line), "%s e220 ping %lu uptime_ms=%lu",
           LORA_NODE_NAME,
           txSequence++,
           millis());
  sendLine(line);
}

static void scheduleAutoSend()
{
  nextAutoSendMs = millis() + LORA_AUTO_SEND_MS + (unsigned long)random(0, 1000);
}
#else
static void scheduleAutoSend()
{
}
#endif

static void handleUsbByte(char ch)
{
  if (ch == '\r') {
    return;
  }

  if (ch == '\n') {
    if (usbLineLength > 0) {
      usbLine[usbLineLength] = '\0';
#if LORA_ALLOW_MANUAL_TX
      sendLine(usbLine);
#else
      Serial.print(F("RX node, manual TX disabled: "));
      Serial.println(usbLine);
#endif
      usbLineLength = 0;
    }
    return;
  }

  if (usbLineLength < sizeof(usbLine) - 1) {
    usbLine[usbLineLength++] = ch;
  }
}

static void handleRadioByte(char ch)
{
  if (ch == '\r') {
    return;
  }

  if (ch == '\n') {
    if (rxLineLength > 0) {
      rxLine[rxLineLength] = '\0';
      Serial.print(F("RX> "));
      Serial.println(rxLine);
      latestRxLineMs = millis();
      rxLineLength = 0;
    }
    return;
  }

  if (rxLineLength < sizeof(rxLine) - 1) {
    rxLine[rxLineLength++] = ch;
  } else {
    rxLineLength = 0;
  }
}

void setup()
{
  Serial.begin(USB_SERIAL_BAUD);
  delay(500);
  randomSeed(micros());

  setupE220NormalMode();
  beginLoraSerial();
  latestRxLineMs = millis();

  Serial.println();
  Serial.println(F("E220-900T22D basic serial test"));
  Serial.print(F("Role: "));
#if LORA_TEST_SENDER
  Serial.println(F("ESP sender"));
#else
  Serial.println(F("Arduino Uno receiver"));
#endif
  Serial.print(F("LoRa UART baud: "));
  Serial.println(LORA_BAUD);
  Serial.print(F("RX pin: "));
  Serial.println(LORA_RX_PIN);
  Serial.print(F("TX pin: "));
  Serial.println(LORA_TX_PIN);
  Serial.print(F("M0 pin: "));
  Serial.println(LORA_E220_M0_PIN);
  Serial.print(F("M1 pin: "));
  Serial.println(LORA_E220_M1_PIN);
  Serial.print(F("AUX pin: "));
  Serial.println(LORA_E220_AUX_PIN);
#if LORA_TEST_SENDER
  Serial.println(F("Type text in the serial monitor and press Enter to send."));
#else
  Serial.println(F("Waiting for LoRa messages. Connect ESP sender on the other E220."));
#endif

  scheduleAutoSend();
}

void loop()
{
  while (Serial.available() > 0) {
    handleUsbByte((char)Serial.read());
  }

  while (loraSerial.available() > 0) {
    handleRadioByte((char)loraSerial.read());
  }

#if LORA_TEST_SENDER && LORA_AUTO_SEND_MS > 0
  if (nextAutoSendMs != 0 && millis() - nextAutoSendMs < 0x80000000UL) {
    sendPing();
    scheduleAutoSend();
  }
#endif

#if !LORA_TEST_SENDER && LORA_RX_IDLE_LOG_MS > 0
  if (millis() - latestRxLineMs >= LORA_RX_IDLE_LOG_MS) {
    Serial.println(F("RX idle: nenhum pacote recebido ainda"));
    latestRxLineMs = millis();
  }
#endif
}
