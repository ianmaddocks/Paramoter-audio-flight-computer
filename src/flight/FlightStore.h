#pragma once
#include <stdint.h>

// Flight state kept in NVS so an in-flight reset or power blip resumes with the same LZ,
// ground reference, flight time and wind instead of treating the current height as ground.
struct FlightRecord {
  uint32_t version;
  uint32_t savedUtc;  // GPS UTC epoch seconds when saved, 0 if unknown
  bool posValid;      // lastLat/lastLon: where the aircraft was when this was saved
  double lastLat;
  double lastLon;
  bool flying;
  uint32_t flightTimeS;

  bool lzValid;
  double lzLat;
  double lzLon;
  bool groundValid;
  float groundAltM;  // LZ elevation in the same datum as baro altitude + offset
  bool offsetInit;
  float offsetM;

  bool takeoffCourseValid;
  float takeoffCourseDeg;

  uint8_t windSource;
  float windFromDeg;
  float windSpeedMs;
  float windAirspeedMs;
};

namespace flightstore {
bool load(FlightRecord& rec);
void save(const FlightRecord& rec);
}  // namespace flightstore
