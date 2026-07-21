#include "bms_mqtt_publisher.h"

#ifndef BMS_MQTT_ENABLE
#define BMS_MQTT_ENABLE 0
#endif

#if BMS_MQTT_ENABLE

#include <Arduino.h>
#include <Client.h>
#include <PubSubClient.h>
#include <WiFi.h>
#include <WiFiManager.h>
#include <string.h>

#include "bms_a7670_modem.h"
#include "bms_telemetry_json.h"
#include "main.h"

#ifndef BMS_MQTT_HOST
#define BMS_MQTT_HOST "broker.hivemq.com"
#endif

#ifndef BMS_MQTT_PORT
#define BMS_MQTT_PORT 1883
#endif

#ifndef BMS_MQTT_TOPIC_BASE
#define BMS_MQTT_TOPIC_BASE "solmar/bms/felicity-fla12171"
#endif

#ifndef BMS_MQTT_CLIENT_PREFIX
#define BMS_MQTT_CLIENT_PREFIX "solmar-bms-gateway"
#endif

#ifndef BMS_MQTT_USERNAME
#define BMS_MQTT_USERNAME ""
#endif

#ifndef BMS_MQTT_PASSWORD
#define BMS_MQTT_PASSWORD ""
#endif

#ifndef BMS_MQTT_RETAIN
#define BMS_MQTT_RETAIN 1
#endif

#ifndef BMS_MQTT_BUFFER_SIZE
#define BMS_MQTT_BUFFER_SIZE 1536
#endif

#ifndef BMS_MQTT_TOPIC_BUFFER_SIZE
#define BMS_MQTT_TOPIC_BUFFER_SIZE 128
#endif

#ifndef BMS_MQTT_RECONNECT_INTERVAL_MS
#define BMS_MQTT_RECONNECT_INTERVAL_MS 5000
#endif

#ifndef BMS_WIFI_RECONNECT_INTERVAL_MS
#define BMS_WIFI_RECONNECT_INTERVAL_MS 15000
#endif

#ifndef BMS_WIFI_CONFIG_PORTAL_TIMEOUT_SECONDS
#define BMS_WIFI_CONFIG_PORTAL_TIMEOUT_SECONDS 120
#endif

#ifndef BMS_WIFI_CONNECT_TIMEOUT_SECONDS
#define BMS_WIFI_CONNECT_TIMEOUT_SECONDS 20
#endif

#ifndef BMS_WIFI_MANAGER_DEBUG
#define BMS_WIFI_MANAGER_DEBUG 0
#endif

#ifndef BMS_WIFI_AP_NAME
#define BMS_WIFI_AP_NAME "Solmar-BMS-Setup"
#endif

#ifndef BMS_WIFI_AP_PASSWORD
#define BMS_WIFI_AP_PASSWORD ""
#endif

#define BMS_INTERNET_SOURCE_WIFI_ONLY 1
#define BMS_INTERNET_SOURCE_A7670_ONLY 2
#define BMS_INTERNET_SOURCE_WIFI_FIRST 3
#define BMS_INTERNET_SOURCE_A7670_FIRST 4

#ifndef BMS_INTERNET_SOURCE
#define BMS_INTERNET_SOURCE BMS_INTERNET_SOURCE_WIFI_ONLY
#endif

#ifndef BMS_MQTT_TOPIC_BUFFER_SIZE
#define BMS_MQTT_TOPIC_BUFFER_SIZE 128
#endif

class BmsNetworkTransport {
public:
  bool begin()
  {
    if (started_) {
      return connected();
    }

    started_ = true;
    WiFi.mode(WIFI_STA);
    manager_.setDebugOutput(BMS_WIFI_MANAGER_DEBUG);
    manager_.setConfigPortalTimeout(BMS_WIFI_CONFIG_PORTAL_TIMEOUT_SECONDS);
    manager_.setConnectTimeout(BMS_WIFI_CONNECT_TIMEOUT_SECONDS);

    const char *portalPassword = strlen(BMS_WIFI_AP_PASSWORD) > 0 ? BMS_WIFI_AP_PASSWORD : nullptr;
    bool ok = manager_.autoConnect(BMS_WIFI_AP_NAME, portalPassword);

    if (ok) {
      writeLog("[NET] WiFi connected: %s RSSI=%d dBm\n",
               WiFi.localIP().toString().c_str(),
               WiFi.RSSI());
    } else {
      writeLog("[NET] WiFi not connected; MQTT will retry in the background\n");
    }

    return ok;
  }

  void loop()
  {
    if (connected()) {
      return;
    }

    uint32_t now = millis();
    if (now - latestReconnectAttemptMs_ < BMS_WIFI_RECONNECT_INTERVAL_MS) {
      return;
    }

    latestReconnectAttemptMs_ = now;
    writeLog("[NET] WiFi reconnect attempt\n");
    WiFi.reconnect();
  }

  bool connected() const
  {
    return WiFi.status() == WL_CONNECTED;
  }

  Client &client()
  {
    return wifiClient_;
  }

  const char *name() const
  {
    return "wifi-manager";
  }

private:
  WiFiClient wifiClient_;
  WiFiManager manager_;
  bool started_ = false;
  uint32_t latestReconnectAttemptMs_ = 0;
};

static BmsNetworkTransport networkTransport;
static PubSubClient mqttClient(networkTransport.client());
static uint32_t mqttSequence = 0;
static uint32_t latestMqttReconnectAttemptMs = 0;
static bool mqttStarted = false;
static char mqttPayloadBuffer[BMS_MQTT_BUFFER_SIZE];
static char mqttTopicBuffer[BMS_MQTT_TOPIC_BUFFER_SIZE];

static bool wifiAllowed()
{
  return BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_WIFI_ONLY ||
         BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_WIFI_FIRST ||
         BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_A7670_FIRST;
}

static bool a7670Allowed()
{
  return BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_A7670_ONLY ||
         BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_WIFI_FIRST ||
         BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_A7670_FIRST;
}

static bool a7670Preferred()
{
  return BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_A7670_ONLY ||
         BMS_INTERNET_SOURCE == BMS_INTERNET_SOURCE_A7670_FIRST;
}

static const char *internetSourceName()
{
  switch (BMS_INTERNET_SOURCE) {
    case BMS_INTERNET_SOURCE_A7670_ONLY:
      return "a7670-only";
    case BMS_INTERNET_SOURCE_WIFI_FIRST:
      return "wifi-first";
    case BMS_INTERNET_SOURCE_A7670_FIRST:
      return "a7670-first";
    case BMS_INTERNET_SOURCE_WIFI_ONLY:
    default:
      return "wifi-only";
  }
}

static void buildClientId(char *buffer, size_t bufferSize)
{
#if defined(ESP32)
  snprintf(buffer, bufferSize, "%s-%08lx", BMS_MQTT_CLIENT_PREFIX, (unsigned long)(ESP.getEfuseMac() & 0xFFFFFFFFUL));
#else
  snprintf(buffer, bufferSize, "%s-%lu", BMS_MQTT_CLIENT_PREFIX, (unsigned long)millis());
#endif
}

static bool mqttCredentialsConfigured()
{
  return strlen(BMS_MQTT_USERNAME) > 0;
}

static bool connectMqtt()
{
  if (!networkTransport.connected()) {
    return false;
  }

  char clientId[64];
  buildClientId(clientId, sizeof(clientId));
  writeLog("[MQTT] Connecting to %s:%u as %s\n", BMS_MQTT_HOST, (unsigned)BMS_MQTT_PORT, clientId);

  bool connected = false;
  if (mqttCredentialsConfigured()) {
    connected = mqttClient.connect(clientId, BMS_MQTT_USERNAME, BMS_MQTT_PASSWORD);
  } else {
    connected = mqttClient.connect(clientId);
  }

  if (!connected) {
    writeLog("[MQTT] Connect failed, state=%d\n", mqttClient.state());
    return false;
  }

  writeLog("[MQTT] Connected, publishing retained readings under %s/readings/v1\n", BMS_MQTT_TOPIC_BASE);
  return true;
}

static bool ensureMqttConnected()
{
  if (!mqttStarted) {
    return false;
  }

  networkTransport.loop();

  if (!networkTransport.connected()) {
    return false;
  }

  if (mqttClient.connected()) {
    return true;
  }

  uint32_t now = millis();
  if (latestMqttReconnectAttemptMs != 0 &&
      now - latestMqttReconnectAttemptMs < BMS_MQTT_RECONNECT_INTERVAL_MS) {
    return false;
  }

  latestMqttReconnectAttemptMs = now;
  return connectMqtt();
}

static bool buildTopic(const BmsMessage &msg)
{
  int written = snprintf(mqttTopicBuffer,
                         sizeof(mqttTopicBuffer),
                         "%s/readings/v1/%u/%s",
                         BMS_MQTT_TOPIC_BASE,
                         (unsigned)msg.deviceId,
                         bmsTelemetryMessageTypeName(msg.type));

  return written > 0 && (size_t)written < sizeof(mqttTopicBuffer);
}

static bool buildRawTopic(const char *relativeTopic)
{
  if (relativeTopic == nullptr) {
    return false;
  }

  int written = snprintf(mqttTopicBuffer,
                         sizeof(mqttTopicBuffer),
                         "%s/%s",
                         BMS_MQTT_TOPIC_BASE,
                         relativeTopic);

  return written > 0 && (size_t)written < sizeof(mqttTopicBuffer);
}

static bool publishWifi(const char *topic, const char *payload, bool retain)
{
  if (!wifiAllowed() || !ensureMqttConnected()) {
    return false;
  }

  bool ok = mqttClient.publish(topic, payload, retain);
  if (!ok) {
    writeLog("[MQTT] WiFi publish failed, topic=%s state=%d\n", topic, mqttClient.state());
    return false;
  }

  return true;
}

static bool publishA7670(const char *topic, const char *payload, bool retain)
{
  if (!a7670Allowed()) {
    return false;
  }

  bool ok = bmsA7670MqttPublish(topic, payload, retain);
  if (!ok) {
    writeLog("[MQTT] A7670 publish failed, topic=%s status=%s\n", topic, bmsA7670StatusText());
  }
  return ok;
}

static bool publishSelectedTransport(const char *topic, const char *payload, bool retain)
{
  if (a7670Preferred()) {
    return publishA7670(topic, payload, retain) || publishWifi(topic, payload, retain);
  }

  return publishWifi(topic, payload, retain) || publishA7670(topic, payload, retain);
}

bool bmsMqttPublisherBegin()
{
  if (mqttStarted) {
    return bmsMqttPublisherIsReady();
  }

  mqttStarted = true;
  writeLog("[MQTT] Internet source: %s\n", internetSourceName());
  mqttClient.setServer(BMS_MQTT_HOST, BMS_MQTT_PORT);
  mqttClient.setBufferSize(BMS_MQTT_BUFFER_SIZE);

  if (wifiAllowed()) {
    writeLog("[MQTT] WiFi backend: %s\n", networkTransport.name());
    networkTransport.begin();
  }

  if (a7670Allowed()) {
    bmsA7670Begin();
  }

  return bmsMqttPublisherIsReady();
}

void bmsMqttPublisherLoop()
{
  if (wifiAllowed() && ensureMqttConnected()) {
    mqttClient.loop();
  }

  if (a7670Allowed()) {
    bmsA7670Loop();
  }
}

bool bmsMqttPublisherHandleMessage(const BmsMessage &msg)
{
  if (!buildTopic(msg)) {
    writeLog("[MQTT] Topic too long for BMS%u %s\n",
             (unsigned)msg.deviceId,
             bmsTelemetryMessageTypeName(msg.type));
    return false;
  }

  if (!bmsTelemetryBuildJson(msg, ++mqttSequence, millis(), mqttPayloadBuffer, sizeof(mqttPayloadBuffer))) {
    writeLog("[MQTT] JSON payload truncated for BMS%u %s\n",
             (unsigned)msg.deviceId,
             bmsTelemetryMessageTypeName(msg.type));
    return false;
  }

  return publishSelectedTransport(mqttTopicBuffer, mqttPayloadBuffer, BMS_MQTT_RETAIN != 0);
}

bool bmsMqttPublisherPublishRaw(const char *relativeTopic, const char *payload, bool retain)
{
  if (payload == nullptr || !buildRawTopic(relativeTopic)) {
    return false;
  }

  return publishSelectedTransport(mqttTopicBuffer, payload, retain);
}

bool bmsMqttPublisherIsReady()
{
  bool wifiReady = wifiAllowed() && networkTransport.connected() && mqttClient.connected();
  bool a7670Ready = a7670Allowed() && bmsA7670IsReady();
  return mqttStarted && (wifiReady || a7670Ready);
}

#else

bool bmsMqttPublisherBegin()
{
  return false;
}

void bmsMqttPublisherLoop()
{
}

bool bmsMqttPublisherHandleMessage(const BmsMessage &msg)
{
  (void)msg;
  return false;
}

bool bmsMqttPublisherPublishRaw(const char *relativeTopic, const char *payload, bool retain)
{
  (void)relativeTopic;
  (void)payload;
  (void)retain;
  return false;
}

bool bmsMqttPublisherIsReady()
{
  return false;
}

#endif
