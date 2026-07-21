#include <Arduino.h>

static const uint32_t LORA_BAUD = 9600;
static const int STATUS_LED_PIN = 13;

static char lineBuffer[96];
static size_t lineLength = 0;
static uint32_t receivedLines = 0;
static uint32_t latestBlinkMs = 0;
static bool ledState = false;

static void blinkReceived()
{
  ledState = true;
  latestBlinkMs = millis();
  digitalWrite(STATUS_LED_PIN, HIGH);
}

static void handleLine()
{
  lineBuffer[lineLength] = '\0';
  receivedLines++;

  blinkReceived();

  Serial.print("UNO receiver ok count=");
  Serial.print((unsigned long)receivedLines);
  Serial.print(" msg=");
  Serial.println(lineBuffer);

  lineLength = 0;
}

static void handleByte(char value)
{
  if (value == '\r') {
    return;
  }

  if (value == '\n') {
    if (lineLength > 0) {
      handleLine();
    }
    return;
  }

  if (lineLength < sizeof(lineBuffer) - 1) {
    lineBuffer[lineLength++] = value;
  } else {
    lineLength = 0;
  }
}

void setup()
{
  pinMode(STATUS_LED_PIN, OUTPUT);
  digitalWrite(STATUS_LED_PIN, LOW);

  Serial.begin(LORA_BAUD);
}

void loop()
{
  while (Serial.available() > 0) {
    handleByte((char)Serial.read());
  }

  if (ledState && millis() - latestBlinkMs >= 120) {
    ledState = false;
    digitalWrite(STATUS_LED_PIN, LOW);
  }
}
