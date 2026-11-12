#pragma once
#include "flight/FlightStore.h"
#include "nav/Nav.h"

class Baro;
struct GpsData;

enum class FlightEvent : uint8_t { None, Takeoff, Landing, Resumed };

// Fuses baro + GPS altitude, tracks the ground/LZ reference and detects takeoff and landing.
class Flight {
 public:
  FlightEvent update(uint32_t nowMs, const Baro& baro, const GpsData& gps, NavState& out);

  // Does the data say the aircraft is standing still on the ground? With a trusted GPS ground speed it
  // is the speed. Without one (no fix, a weak fix, a stale fix) the speed says nothing, so the evidence
  // is a steady barometer (no climb or sink) AND a height above the landing zone under maxAglM; with
  // no barometer either there is no evidence and the answer is no. Level flight with the GPS lost is
  // therefore never taken for a landing.
  static bool looksStill(bool speedOk, float speedMs, float landingSpeedMs, bool baroOk, float varioMs, float aglM,
                         float maxAglM);

  bool takeoffCourseValid() const { return takeoffCourseValid_; }
  float takeoffCourseDeg() const { return takeoffCourseDeg_; }

  void exportState(FlightRecord& rec) const;
  // Restores a saved record at boot; a saved flight is reported as FlightEvent::Resumed.
  void importState(const FlightRecord& rec, uint32_t nowMs);
  void addFlightTime(uint32_t seconds) { takeoffMs_ -= seconds * 1000; }
  // Drops a restored flight that turned out to be stale, without a landing event.
  void abandonFlight();

 private:
  bool resumePending_ = false;
  bool flying_ = false;
  uint32_t lastMs_ = 0;
  uint32_t takeoffMs_ = 0;
  uint32_t lastFlightTimeS_ = 0;

  float offset_ = 0;  // GPS MSL minus baro standard altitude
  bool offsetInit_ = false;
  float lastAlt_ = 0;

  bool groundValid_ = false;
  float groundAlt_ = 0;
  bool groundStdValid_ = false;
  float groundStd_ = 0;  // baro-only ground reference for takeoff height gain
  bool autoLzValid_ = false;
  double autoLzLat_ = 0, autoLzLon_ = 0;

  uint32_t fastSinceMs_ = 0;
  float courseE_ = 0, courseN_ = 0;
  uint32_t stillSinceMs_ = 0;
  bool takeoffCourseValid_ = false;
  float takeoffCourseDeg_ = 0;
};
