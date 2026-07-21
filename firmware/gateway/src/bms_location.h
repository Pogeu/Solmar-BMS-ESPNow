#ifndef SOLMAR_BMS_LOCATION_H
#define SOLMAR_BMS_LOCATION_H

#include <Arduino.h>

#include "bms_a7670_modem.h"

enum class BmsLocationSource : uint8_t {
  None = 0,
  A7670 = 1,
  Neo6m = 2,
};

struct BmsLocationState {
  BmsGnssFix a7670 = {};
  BmsGnssFix neo6m = {};
  BmsGnssFix selected = {};
  BmsLocationSource selectedSource = BmsLocationSource::None;
};

bool bmsLocationBegin();
void bmsLocationLoop();
const BmsLocationState &bmsLocationState();
bool bmsLocationBuildJson(char *buffer, size_t bufferSize);
const char *bmsLocationSourceName(BmsLocationSource source);

#endif // SOLMAR_BMS_LOCATION_H
