#include <Arduino.h>

static const uint32_t USB_BAUD = 115200;
static const uint32_t LORA_BAUD = 9600;
static const int LORA_RX_PIN = 20;
static const int LORA_TX_PIN = 21;
static const uint32_t SEND_INTERVAL_MS = 3000;

HardwareSerial LoraSerial(1);

static uint32_t sequenceNumber = 0;
static uint32_t latestSendMs = 0;

static void sendPing()
{
  char line[96];
  snprintf(line, sizeof(line), "ESP32C3 E220 sender seq=%lu uptime_ms=%lu",
           (unsigned long)sequenceNumber++,
           (unsigned long)millis());

  LoraSerial.println(line);
  LoraSerial.flush();

  Serial.print("TX> ");
  Serial.println(line);
}

void setup()
{
  Serial.begin(USB_BAUD);
  delay(800);

  LoraSerial.begin(LORA_BAUD, SERIAL_8N1, LORA_RX_PIN, LORA_TX_PIN);

  Serial.println();
  Serial.println("E220 sender ESP32-C3");
  Serial.println("E220 em modo normal: M0=GND, M1=GND");
  Serial.print("LoRa UART baud: ");
  Serial.println(LORA_BAUD);
  Serial.print("ESP32-C3 RX GPIO: ");
  Serial.println(LORA_RX_PIN);
  Serial.print("ESP32-C3 TX GPIO: ");
  Serial.println(LORA_TX_PIN);
}

void loop()
{
  while (LoraSerial.available() > 0) {
    Serial.write((uint8_t)LoraSerial.read());
  }

  uint32_t now = millis();
  if (latestSendMs == 0 || now - latestSendMs >= SEND_INTERVAL_MS) {
    latestSendMs = now;
    sendPing();
  }
}
