#pragma once
#include <stdint.h>

// Snapshot of everything the flight logic needs, rebuilt every tick by Flight::update().
struct NavState {
  uint32_t ms = 0;

  bool fix = false;
  bool gpsGood = false;  // fix with enough satellites, low HDOP and no recent jump
  double lat = 0.0;
  double lon = 0.0;
  float speedMs = 0.0f;
  float trackDeg = 0.0f;
  float gpsAltM = 0.0f;
  uint8_t sats = 0;
  float hdop = 99.0f;

  bool baroOk = false;
  float pressurePa = 0.0f;
  float tempC = 0.0f;
  float altStdM = 0.0f;
  float varioMs = 0.0f;

  float altMslM = 0.0f;
  float aglM = 0.0f;

  bool lzValid = false;
  double lzLat = 0.0;
  double lzLon = 0.0;
  float lzElevM = 0.0f;

  bool flying = false;
  uint32_t flightTimeS = 0;
};
