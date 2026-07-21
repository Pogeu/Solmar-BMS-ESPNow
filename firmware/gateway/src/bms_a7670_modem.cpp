#include "bms_a7670_modem.h"

#ifndef BMS_A7670_ENABLE
#define BMS_A7670_ENABLE 0
#endif

#if BMS_A7670_ENABLE

#include <HardwareSerial.h>
#include <string.h>

#include "bms_ota.h"
#include "main.h"

#ifndef BMS_A7670_UART_NUM
#define BMS_A7670_UART_NUM 0
#endif

#ifndef BMS_A7670_BAUD
#define BMS_A7670_BAUD 115200
#endif

#ifndef BMS_A7670_RX_PIN
#define BMS_A7670_RX_PIN 9
#endif

#ifndef BMS_A7670_TX_PIN
#define BMS_A7670_TX_PIN 10
#endif

#ifndef BMS_A7670_PWRKEY_PIN
#define BMS_A7670_PWRKEY_PIN -1
#endif

#ifndef BMS_A7670_RESET_PIN
#define BMS_A7670_RESET_PIN -1
#endif

#ifndef BMS_A7670_APN
#define BMS_A7670_APN "zap.vivo.com.br"
#endif

#ifndef BMS_A7670_APN_USER
#define BMS_A7670_APN_USER ""
#endif

#ifndef BMS_A7670_APN_PASS
#define BMS_A7670_APN_PASS ""
#endif

#ifndef BMS_A7670_MQTT_CLIENT_ID
#define BMS_A7670_MQTT_CLIENT_ID "solmar-bms-a7670"
#endif

#ifndef BMS_A7670_COMMAND_TIMEOUT_MS
#define BMS_A7670_COMMAND_TIMEOUT_MS 5000
#endif

#ifndef BMS_A7670_MQTT_TIMEOUT_MS
#define BMS_A7670_MQTT_TIMEOUT_MS 15000
#endif

#ifndef BMS_MQTT_HOST
#define BMS_MQTT_HOST "broker.hivemq.com"
#endif

#ifndef BMS_MQTT_PORT
#define BMS_MQTT_PORT 1883
#endif

#ifndef BMS_MQTT_USERNAME
#define BMS_MQTT_USERNAME ""
#endif

#ifndef BMS_MQTT_PASSWORD
#define BMS_MQTT_PASSWORD ""
#endif

static HardwareSerial a7670Serial(BMS_A7670_UART_NUM);
static bool modemStarted = false;
static bool modemReady = false;
static bool mqttStarted = false;
static bool mqttConnected = false;
static uint32_t latestReadyCheckMs = 0;
static char latestStatus[40] = "off";

static bool configuredPin(int pin)
{
  return pin >= 0;
}

static void setStatus(const char *status)
{
  strncpy(latestStatus, status, sizeof(latestStatus) - 1);
  latestStatus[sizeof(latestStatus) - 1] = '\0';
}

static void drainSerial()
{
  while (a7670Serial.available()) {
    a7670Serial.read();
  }
}

static bool waitForToken(const char *token, uint32_t timeoutMs, char *response = nullptr, size_t responseSize = 0)
{
  uint32_t startMs = millis();
  char localResponse[180];
  char *target = response;
  size_t targetSize = responseSize;
  size_t len = 0;

  if (target == nullptr || targetSize == 0) {
    target = localResponse;
    targetSize = sizeof(localResponse);
  }

  if (targetSize < 2) {
    return false;
  }

  target[0] = '\0';

  auto appendChar = [&](char c) {
    if (len < targetSize - 1) {
      target[len++] = c;
      target[len] = '\0';
      return;
    }

    memmove(target, target + 1, targetSize - 2);
    target[targetSize - 2] = c;
    target[targetSize - 1] = '\0';
  };

  auto tokenSeen = [&]() {
    return strstr(target, token) != nullptr;
  };

  if (tokenSeen()) {
    return true;
  }

  while (millis() - startMs < timeoutMs) {
    while (a7670Serial.available()) {
      char c = static_cast<char>(a7670Serial.read());
      appendChar(c);
    }

    if (tokenSeen()) {
      return true;
    }
    bmsOtaLoop();
    delay(5);
  }

  return false;
}

static bool sendCommand(const char *cmd, const char *expected, uint32_t timeoutMs,
                        char *response = nullptr, size_t responseSize = 0)
{
  drainSerial();
  a7670Serial.print(cmd);
  a7670Serial.print("\r\n");
  return waitForToken(expected, timeoutMs, response, responseSize);
}

static bool sendDataAfterPrompt(const char *cmd, const char *data, uint32_t timeoutMs)
{
  drainSerial();
  a7670Serial.print(cmd);
  a7670Serial.print("\r\n");
  if (!waitForToken(">", timeoutMs)) {
    return false;
  }
  a7670Serial.print(data);
  return waitForToken("OK", timeoutMs);
}

static void pulsePwrKey()
{
  if (!configuredPin(BMS_A7670_PWRKEY_PIN)) {
    return;
  }

  pinMode(BMS_A7670_PWRKEY_PIN, OUTPUT);
  digitalWrite(BMS_A7670_PWRKEY_PIN, LOW);
  delay(1200);
  digitalWrite(BMS_A7670_PWRKEY_PIN, HIGH);
  delay(4000);
}

static double nmeaDegreesToDecimal(const char *value, char hemisphere)
{
  if (value == nullptr || value[0] == '\0') {
    return 0.0;
  }

  double raw = atof(value);
  int degrees = static_cast<int>(raw / 100.0);
  double minutes = raw - (degrees * 100.0);
  double decimal = degrees + (minutes / 60.0);
  if (hemisphere == 'S' || hemisphere == 'W') {
    decimal = -decimal;
  }
  return decimal;
}

static bool parseCgpsInfo(char *response, BmsGnssFix *fix)
{
  char *line = strstr(response, "+CGPSINFO:");
  if (line == nullptr || fix == nullptr) {
    return false;
  }

  line += strlen("+CGPSINFO:");
  while (*line == ' ') {
    line++;
  }

  char *fields[9] = {};
  uint8_t count = 0;
  fields[count++] = line;
  for (char *p = line; *p != '\0' && count < 9; ++p) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    } else if (*p == '\r' || *p == '\n') {
      *p = '\0';
      break;
    }
  }

  if (count < 4 || fields[0][0] == '\0' || fields[2][0] == '\0') {
    return false;
  }

  fix->valid = true;
  fix->latitude = nmeaDegreesToDecimal(fields[0], fields[1][0]);
  fix->longitude = nmeaDegreesToDecimal(fields[2], fields[3][0]);
  fix->altitudeM = count > 6 && fields[6][0] != '\0' ? atof(fields[6]) : 0.0f;
  fix->speedKmh = count > 7 && fields[7][0] != '\0' ? atof(fields[7]) * 1.852f : 0.0f;
  fix->updatedAtMs = millis();
  return true;
}

static bool ensureModemReady()
{
  if (!modemStarted) {
    bmsA7670Begin();
  }

  if (modemReady) {
    return true;
  }

  uint32_t now = millis();
  if (latestReadyCheckMs != 0 && now - latestReadyCheckMs < 5000) {
    return false;
  }
  latestReadyCheckMs = now;

  if (!sendCommand("AT", "OK", BMS_A7670_COMMAND_TIMEOUT_MS)) {
    setStatus("no-at");
    return false;
  }

  sendCommand("ATE0", "OK", BMS_A7670_COMMAND_TIMEOUT_MS);
  sendCommand("AT+CGPS=1", "OK", BMS_A7670_COMMAND_TIMEOUT_MS);
  sendCommand("AT+CGNSSPWR=1", "OK", BMS_A7670_COMMAND_TIMEOUT_MS);
  modemReady = true;
  setStatus("ready");
  return true;
}

static bool ensurePacketNetwork()
{
  char cmd[120];

  if (!ensureModemReady()) {
    return false;
  }

  sendCommand("AT+CPIN?", "READY", BMS_A7670_COMMAND_TIMEOUT_MS);
  sendCommand("AT+CGATT=1", "OK", 10000);
  snprintf(cmd, sizeof(cmd), "AT+CGDCONT=1,\"IP\",\"%s\"", BMS_A7670_APN);
  sendCommand(cmd, "OK", BMS_A7670_COMMAND_TIMEOUT_MS);

  sendCommand("AT+NETCLOSE", "OK", 3000);
  if (!sendCommand("AT+NETOPEN", "OK", 12000)) {
    setStatus("net-fail");
    return false;
  }

  setStatus("net-open");
  return true;
}

static bool ensureMqttConnected()
{
  char cmd[180];

  if (mqttConnected) {
    return true;
  }

  if (!ensurePacketNetwork()) {
    return false;
  }

  if (!mqttStarted) {
    sendCommand("AT+CMQTTSTOP", "OK", 3000);
    if (!sendCommand("AT+CMQTTSTART", "OK", BMS_A7670_MQTT_TIMEOUT_MS)) {
      setStatus("mqtt-start-fail");
      return false;
    }

    snprintf(cmd, sizeof(cmd), "AT+CMQTTACCQ=0,\"%s\",0", BMS_A7670_MQTT_CLIENT_ID);
    if (!sendCommand(cmd, "OK", BMS_A7670_COMMAND_TIMEOUT_MS)) {
      setStatus("mqtt-accq-fail");
      return false;
    }
    mqttStarted = true;
  }

  if (strlen(BMS_MQTT_USERNAME) > 0) {
    snprintf(cmd,
             sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",60,1,\"%s\",\"%s\"",
             BMS_MQTT_HOST,
             (unsigned)BMS_MQTT_PORT,
             BMS_MQTT_USERNAME,
             BMS_MQTT_PASSWORD);
  } else {
    snprintf(cmd,
             sizeof(cmd),
             "AT+CMQTTCONNECT=0,\"tcp://%s:%u\",60,1",
             BMS_MQTT_HOST,
             (unsigned)BMS_MQTT_PORT);
  }

  if (!sendCommand(cmd, "OK", BMS_A7670_MQTT_TIMEOUT_MS)) {
    mqttConnected = false;
    setStatus("mqtt-connect-fail");
    return false;
  }

  mqttConnected = true;
  setStatus("mqtt");
  return true;
}

bool bmsA7670Begin()
{
  if (modemStarted) {
    return ensureModemReady();
  }

  modemStarted = true;
  setStatus("boot");
  if (configuredPin(BMS_A7670_RESET_PIN)) {
    pinMode(BMS_A7670_RESET_PIN, OUTPUT);
    digitalWrite(BMS_A7670_RESET_PIN, HIGH);
  }

  a7670Serial.begin(BMS_A7670_BAUD, SERIAL_8N1, BMS_A7670_RX_PIN, BMS_A7670_TX_PIN);
  pulsePwrKey();
  writeLog("[A7670] UART%d RX=GPIO%d TX=GPIO%d baud=%lu\n",
           BMS_A7670_UART_NUM,
           BMS_A7670_RX_PIN,
           BMS_A7670_TX_PIN,
           (unsigned long)BMS_A7670_BAUD);

  return ensureModemReady();
}

void bmsA7670Loop()
{
  ensureModemReady();
}

bool bmsA7670IsReady()
{
  return ensureModemReady();
}

bool bmsA7670ReadGnssFix(BmsGnssFix *fix)
{
  if (!ensureModemReady() || fix == nullptr) {
    return false;
  }

  char response[220];
  if (!sendCommand("AT+CGPSINFO", "OK", BMS_A7670_COMMAND_TIMEOUT_MS, response, sizeof(response))) {
    return false;
  }

  return parseCgpsInfo(response, fix);
}

bool bmsA7670MqttPublish(const char *topic, const char *payload, bool retain)
{
  if (topic == nullptr || payload == nullptr || !ensureMqttConnected()) {
    return false;
  }

  char cmd[64];
  snprintf(cmd, sizeof(cmd), "AT+CMQTTTOPIC=0,%u", (unsigned)strlen(topic));
  if (!sendDataAfterPrompt(cmd, topic, BMS_A7670_COMMAND_TIMEOUT_MS)) {
    mqttConnected = false;
    setStatus("mqtt-topic-fail");
    return false;
  }

  snprintf(cmd, sizeof(cmd), "AT+CMQTTPAYLOAD=0,%u", (unsigned)strlen(payload));
  if (!sendDataAfterPrompt(cmd, payload, BMS_A7670_COMMAND_TIMEOUT_MS)) {
    mqttConnected = false;
    setStatus("mqtt-payload-fail");
    return false;
  }

  snprintf(cmd, sizeof(cmd), "AT+CMQTTPUB=0,0,60,%u", retain ? 1U : 0U);
  if (!sendCommand(cmd, "OK", BMS_A7670_MQTT_TIMEOUT_MS)) {
    mqttConnected = false;
    setStatus("mqtt-pub-fail");
    return false;
  }

  return true;
}

const char *bmsA7670StatusText()
{
  return latestStatus;
}

#else

bool bmsA7670Begin()
{
  return false;
}

void bmsA7670Loop()
{
}

bool bmsA7670IsReady()
{
  return false;
}

bool bmsA7670ReadGnssFix(BmsGnssFix *fix)
{
  (void)fix;
  return false;
}

bool bmsA7670MqttPublish(const char *topic, const char *payload, bool retain)
{
  (void)topic;
  (void)payload;
  (void)retain;
  return false;
}

const char *bmsA7670StatusText()
{
  return "disabled";
}

#endif
