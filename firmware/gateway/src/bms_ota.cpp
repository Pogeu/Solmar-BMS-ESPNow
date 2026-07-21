#include "bms_ota.h"

#ifndef BMS_OTA_ENABLE
#define BMS_OTA_ENABLE 0
#endif

#if BMS_OTA_ENABLE

#include <Arduino.h>
#include <ArduinoOTA.h>
#include <WiFi.h>
#include <string.h>

#include "main.h"

#ifndef BMS_OTA_HOSTNAME
#define BMS_OTA_HOSTNAME "solmar-central"
#endif

#ifndef BMS_OTA_PASSWORD
#define BMS_OTA_PASSWORD ""
#endif

#ifndef BMS_OTA_RETRY_LOG_INTERVAL_MS
#define BMS_OTA_RETRY_LOG_INTERVAL_MS 30000
#endif

static bool otaStarted = false;
static uint32_t latestRetryLogMs = 0;

static bool wifiReady()
{
  return WiFi.status() == WL_CONNECTED;
}

static void startOta()
{
  if (otaStarted || !wifiReady()) {
    return;
  }

  ArduinoOTA.setHostname(BMS_OTA_HOSTNAME);
  if (strlen(BMS_OTA_PASSWORD) > 0) {
    ArduinoOTA.setPassword(BMS_OTA_PASSWORD);
  }

  ArduinoOTA.onStart([]() {
    const char *kind = ArduinoOTA.getCommand() == U_FLASH ? "firmware" : "filesystem";
    writeLog("[OTA] Start %s update\n", kind);
  });

  ArduinoOTA.onEnd([]() {
    writeLog("[OTA] Update finished\n");
  });

  ArduinoOTA.onProgress([](unsigned int progress, unsigned int total) {
    if (total == 0) {
      return;
    }
    static unsigned int latestPercent = 101;
    unsigned int percent = (progress * 100U) / total;
    if (percent != latestPercent && percent % 10U == 0) {
      latestPercent = percent;
      writeLog("[OTA] Progress %u%%\n", percent);
    }
  });

  ArduinoOTA.onError([](ota_error_t error) {
    writeLog("[OTA] Error %u\n", static_cast<unsigned>(error));
  });

  ArduinoOTA.begin();
  otaStarted = true;
  writeLog("[OTA] Ready hostname=%s ip=%s auth=%s\n",
           BMS_OTA_HOSTNAME,
           WiFi.localIP().toString().c_str(),
           strlen(BMS_OTA_PASSWORD) > 0 ? "enabled" : "disabled");
}

bool bmsOtaBegin()
{
  startOta();
  return otaStarted;
}

void bmsOtaLoop()
{
  if (!otaStarted) {
    startOta();
    if (!otaStarted) {
      uint32_t now = millis();
      if (latestRetryLogMs == 0 || now - latestRetryLogMs >= BMS_OTA_RETRY_LOG_INTERVAL_MS) {
        latestRetryLogMs = now;
        writeLog("[OTA] Waiting for WiFi before enabling OTA\n");
      }
      return;
    }
  }

  ArduinoOTA.handle();
}

bool bmsOtaIsReady()
{
  return otaStarted && wifiReady();
}

#else

bool bmsOtaBegin()
{
  return false;
}

void bmsOtaLoop()
{
}

bool bmsOtaIsReady()
{
  return false;
}

#endif
