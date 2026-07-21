#include "bms_location.h"

#ifndef BMS_LOCATION_ENABLE
#define BMS_LOCATION_ENABLE 0
#endif

#if BMS_LOCATION_ENABLE

#include <HardwareSerial.h>
#include <string.h>

#ifndef BMS_NEO6M_ENABLE
#define BMS_NEO6M_ENABLE 1
#endif

#ifndef BMS_NEO6M_USE_SOFTWARE_SERIAL
#define BMS_NEO6M_USE_SOFTWARE_SERIAL 0
#endif

#if BMS_NEO6M_USE_SOFTWARE_SERIAL
#include <SoftwareSerial.h>
#endif

#include "main.h"

#ifndef BMS_NEO6M_UART_NUM
#define BMS_NEO6M_UART_NUM 2
#endif

#ifndef BMS_NEO6M_BAUD
#define BMS_NEO6M_BAUD 9600
#endif

#ifndef BMS_NEO6M_RX_PIN
#define BMS_NEO6M_RX_PIN 15
#endif

#ifndef BMS_NEO6M_TX_PIN
#define BMS_NEO6M_TX_PIN -1
#endif

#ifndef BMS_LOCATION_FIX_STALE_MS
#define BMS_LOCATION_FIX_STALE_MS 30000
#endif

#ifndef BMS_A7670_GNSS_POLL_MS
#define BMS_A7670_GNSS_POLL_MS 5000
#endif

static BmsLocationState locationState;
static uint32_t latestA7670PollMs = 0;
static char nmeaLine[120];
static uint8_t nmeaLineLen = 0;

#if BMS_NEO6M_ENABLE
#if BMS_NEO6M_USE_SOFTWARE_SERIAL
static SoftwareSerial neoSerial;
#else
static HardwareSerial neoSerial(BMS_NEO6M_UART_NUM);
#endif
#endif

static bool fixFresh(const BmsGnssFix &fix)
{
  return fix.valid && millis() - fix.updatedAtMs <= BMS_LOCATION_FIX_STALE_MS;
}

const char *bmsLocationSourceName(BmsLocationSource source)
{
  switch (source) {
    case BmsLocationSource::A7670:
      return "a7670";
    case BmsLocationSource::Neo6m:
      return "neo6m";
    case BmsLocationSource::None:
    default:
      return "none";
  }
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

static uint8_t splitCsv(char *line, char **fields, uint8_t maxFields)
{
  uint8_t count = 0;
  fields[count++] = line;
  for (char *p = line; *p != '\0' && count < maxFields; ++p) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    } else if (*p == '*') {
      *p = '\0';
      break;
    }
  }
  return count;
}

static void parseRmc(char *line)
{
  char *fields[13] = {};
  uint8_t count = splitCsv(line, fields, 13);
  if (count < 8 || fields[2][0] != 'A') {
    return;
  }

  locationState.neo6m.valid = true;
  locationState.neo6m.latitude = nmeaDegreesToDecimal(fields[3], fields[4][0]);
  locationState.neo6m.longitude = nmeaDegreesToDecimal(fields[5], fields[6][0]);
  locationState.neo6m.speedKmh = atof(fields[7]) * 1.852f;
  locationState.neo6m.updatedAtMs = millis();
}

static void parseGga(char *line)
{
  char *fields[15] = {};
  uint8_t count = splitCsv(line, fields, 15);
  if (count < 10 || atoi(fields[6]) == 0) {
    return;
  }

  locationState.neo6m.valid = true;
  locationState.neo6m.latitude = nmeaDegreesToDecimal(fields[2], fields[3][0]);
  locationState.neo6m.longitude = nmeaDegreesToDecimal(fields[4], fields[5][0]);
  locationState.neo6m.satellites = static_cast<uint8_t>(atoi(fields[7]));
  locationState.neo6m.altitudeM = atof(fields[9]);
  locationState.neo6m.updatedAtMs = millis();
}

static void parseNmeaLine(char *line)
{
  if (strncmp(line, "$GPRMC", 6) == 0 || strncmp(line, "$GNRMC", 6) == 0) {
    parseRmc(line);
  } else if (strncmp(line, "$GPGGA", 6) == 0 || strncmp(line, "$GNGGA", 6) == 0) {
    parseGga(line);
  }
}

static void pollNeo6m()
{
#if BMS_NEO6M_ENABLE
  while (neoSerial.available()) {
    char c = static_cast<char>(neoSerial.read());
    if (c == '\n') {
      nmeaLine[nmeaLineLen] = '\0';
      if (nmeaLineLen > 6) {
        parseNmeaLine(nmeaLine);
      }
      nmeaLineLen = 0;
    } else if (c != '\r' && nmeaLineLen < sizeof(nmeaLine) - 1) {
      nmeaLine[nmeaLineLen++] = c;
    }
  }
#endif
}

static void pollA7670Gnss()
{
  uint32_t now = millis();
  if (latestA7670PollMs != 0 && now - latestA7670PollMs < BMS_A7670_GNSS_POLL_MS) {
    return;
  }
  latestA7670PollMs = now;

  BmsGnssFix fix;
  if (bmsA7670ReadGnssFix(&fix)) {
    locationState.a7670 = fix;
  }
}

static void chooseLocation()
{
  if (fixFresh(locationState.a7670)) {
    locationState.selected = locationState.a7670;
    locationState.selectedSource = BmsLocationSource::A7670;
  } else if (fixFresh(locationState.neo6m)) {
    locationState.selected = locationState.neo6m;
    locationState.selectedSource = BmsLocationSource::Neo6m;
  } else {
    locationState.selected.valid = false;
    locationState.selectedSource = BmsLocationSource::None;
  }
}

bool bmsLocationBegin()
{
#if BMS_NEO6M_ENABLE
#if BMS_NEO6M_USE_SOFTWARE_SERIAL
  neoSerial.begin(BMS_NEO6M_BAUD, SWSERIAL_8N1, BMS_NEO6M_RX_PIN, BMS_NEO6M_TX_PIN, false);
#else
  neoSerial.begin(BMS_NEO6M_BAUD, SERIAL_8N1, BMS_NEO6M_RX_PIN, BMS_NEO6M_TX_PIN);
#endif
  writeLog("[GPS] NEO-6M RX=GPIO%d TX=GPIO%d baud=%lu%s\n",
           BMS_NEO6M_RX_PIN,
           BMS_NEO6M_TX_PIN,
           (unsigned long)BMS_NEO6M_BAUD,
           BMS_NEO6M_USE_SOFTWARE_SERIAL ? " software-serial" : "");
#endif

  bmsA7670Begin();
  return true;
}

void bmsLocationLoop()
{
  bmsA7670Loop();
  pollNeo6m();
  pollA7670Gnss();
  chooseLocation();
}

const BmsLocationState &bmsLocationState()
{
  return locationState;
}

bool bmsLocationBuildJson(char *buffer, size_t bufferSize)
{
  if (buffer == nullptr || bufferSize == 0) {
    return false;
  }

  const BmsGnssFix &fix = locationState.selected;
  int written = snprintf(buffer,
                         bufferSize,
                         "{\"schema\":\"solmar.location.v1\",\"uptime_ms\":%lu,"
                         "\"source\":\"%s\",\"valid\":%s,\"latitude\":%.7f,"
                         "\"longitude\":%.7f,\"altitude_m\":%.1f,\"speed_kmh\":%.1f,"
                         "\"satellites\":%u,\"age_ms\":%lu,"
                         "\"a7670_valid\":%s,\"neo6m_valid\":%s}",
                         (unsigned long)millis(),
                         bmsLocationSourceName(locationState.selectedSource),
                         fix.valid ? "true" : "false",
                         fix.latitude,
                         fix.longitude,
                         fix.altitudeM,
                         fix.speedKmh,
                         (unsigned)fix.satellites,
                         fix.valid ? (unsigned long)(millis() - fix.updatedAtMs) : 0UL,
                         fixFresh(locationState.a7670) ? "true" : "false",
                         fixFresh(locationState.neo6m) ? "true" : "false");

  return written > 0 && static_cast<size_t>(written) < bufferSize;
}

#else

bool bmsLocationBegin()
{
  return false;
}

void bmsLocationLoop()
{
}

const BmsLocationState &bmsLocationState()
{
  static BmsLocationState disabledState;
  return disabledState;
}

bool bmsLocationBuildJson(char *buffer, size_t bufferSize)
{
  if (buffer == nullptr || bufferSize == 0) {
    return false;
  }

  int written = snprintf(buffer, bufferSize, "{\"schema\":\"solmar.location.v1\",\"valid\":false}");
  return written > 0 && static_cast<size_t>(written) < bufferSize;
}

const char *bmsLocationSourceName(BmsLocationSource source)
{
  (void)source;
  return "disabled";
}

#endif
