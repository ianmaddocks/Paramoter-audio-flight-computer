#pragma once
#include <stdint.h>

// What the GPS receiver reported, as plain values. Gps::read() fills one every pass of the main
// loop; without a GPS it simply stays at these defaults ("no link, no fix"). The flight logic,
// logger, Bluetooth feed and web page read this and never touch the receiver driver, so they
// work unchanged on a vario that has no GPS.
// A position the receiver reports can only be believed if it is on the globe: latitude within +-90, longitude
// within +-180, not a number, and not exactly 0,0 (what a module with no position often reports). Anything
// else is treated as no fix (F5).
inline bool gpsCoordsValid(double lat, double lon) {
  return lat >= -90.0 && lat <= 90.0 && lon >= -180.0 && lon <= 180.0 && !(lat == 0.0 && lon == 0.0);  // false for NaN
}

struct GpsData {
  bool link = false;    // receiver found and sending data
  bool fix = false;     // recent valid position with at least 4 satellites
  bool steady = false;  // fixes arriving regularly, no position or speed jump for a few seconds
  uint8_t sats = 0;
  float hdop = 99.0f;
  double lat = 0.0;
  double lon = 0.0;
  float speedMs = 0.0f;
  float courseDeg = 0.0f;
  bool altValid = false;
  float altM = 0.0f;

  bool timeValid = false;  // UTC time of day known
  bool dateValid = false;
  uint16_t year = 0;
  uint8_t month = 0, day = 0;
  uint8_t hour = 0, minute = 0, second = 0, centisecond = 0;
  uint32_t utcEpoch = 0;  // seconds since 1970, 0 if the date or time is unknown
};
