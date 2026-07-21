#ifndef SOLMAR_BMS_A7670_MODEM_H
#define SOLMAR_BMS_A7670_MODEM_H

#include <Arduino.h>

struct BmsGnssFix {
  bool valid = false;
  double latitude = 0.0;
  double longitude = 0.0;
  float altitudeM = 0.0f;
  float speedKmh = 0.0f;
  uint8_t satellites = 0;
  uint32_t updatedAtMs = 0;
};

bool bmsA7670Begin();
void bmsA7670Loop();
bool bmsA7670IsReady();
bool bmsA7670ReadGnssFix(BmsGnssFix *fix);
bool bmsA7670MqttPublish(const char *topic, const char *payload, bool retain);
const char *bmsA7670StatusText();

#endif // SOLMAR_BMS_A7670_MODEM_H
