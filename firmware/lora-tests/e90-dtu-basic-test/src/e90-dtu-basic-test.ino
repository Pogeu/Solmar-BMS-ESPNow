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

#ifdef LORA_ON_ESP32
HardwareSerial loraSerial(2);
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

#ifndef LORA_USE_RS485_DRIVER
#define LORA_USE_RS485_DRIVER 1
#endif

#ifndef LORA_RS485_DE_RE_PIN
#define LORA_RS485_DE_RE_PIN 4
#endif

#if !LORA_USE_RS485_DRIVER
#undef LORA_RS485_DE_RE_PIN
#define LORA_RS485_DE_RE_PIN -1
#endif

#ifndef LORA_AUTO_SEND_MS
#define LORA_AUTO_SEND_MS 3000
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
static unsigned long txSequence = 0;
static unsigned long nextAutoSendMs = 0;

static bool configuredPin(int pin)
{
  return pin >= 0;
}

static void setRs485Transmit(bool enabled)
{
  if (!configuredPin(LORA_RS485_DE_RE_PIN)) {
    return;
  }

  digitalWrite(LORA_RS485_DE_RE_PIN, enabled ? HIGH : LOW);
  delay(2);
}

static void beginLoraSerial()
{
#ifdef LORA_ON_ESP32
  loraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);
#else
  loraSerial.begin(LORA_BAUD);
#endif
}

static void sendLine(const char *line)
{
  setRs485Transmit(true);
  loraSerial.print(line);
  loraSerial.print('\n');
  loraSerial.flush();
  setRs485Transmit(false);

  Serial.print(F("TX> "));
  Serial.println(line);
}

static void sendPing()
{
  char line[96];
  snprintf(line, sizeof(line), "%s e90 ping %lu uptime_ms=%lu",
           LORA_NODE_NAME,
           txSequence++,
           millis());
  sendLine(line);
}

static void scheduleAutoSend()
{
#if LORA_AUTO_SEND_MS > 0
  nextAutoSendMs = millis() + LORA_AUTO_SEND_MS + (unsigned long)random(0, 1000);
#endif
}

static void handleUsbByte(char ch)
{
  if (ch == '\r') {
    return;
  }

  if (ch == '\n') {
    if (usbLineLength > 0) {
      usbLine[usbLineLength] = '\0';
      sendLine(usbLine);
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

  if (configuredPin(LORA_RS485_DE_RE_PIN)) {
    pinMode(LORA_RS485_DE_RE_PIN, OUTPUT);
    setRs485Transmit(false);
  }

  beginLoraSerial();

  Serial.println();
  Serial.println(F("E90-DTU basic serial test"));
  Serial.print(F("LoRa UART baud: "));
  Serial.println(LORA_BAUD);
  Serial.print(F("RX pin: "));
  Serial.println(LORA_RX_PIN);
  Serial.print(F("TX pin: "));
  Serial.println(LORA_TX_PIN);
  Serial.print(F("RS485 DE/RE pin: "));
  Serial.println(LORA_RS485_DE_RE_PIN);
  Serial.println(F("Type text in the serial monitor and press Enter to send."));

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

#if LORA_AUTO_SEND_MS > 0
  if (nextAutoSendMs != 0 && millis() - nextAutoSendMs < 0x80000000UL) {
    sendPing();
    scheduleAutoSend();
  }
#endif
}
