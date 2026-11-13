#pragma once
#include <stdint.h>

#include "flight/FlightStore.h"
#include "nav/Geo.h"

// Whether a flight saved before the vario was switched off (or reset) in the air should be resumed. The risk this guards
// against is a vario that wrongly believes it is flying and cannot get out of it, so a flight is resumed only on
// positive evidence that it is still in the air, decided once, at the first trusted GPS fix after power-up:
//
//   * the record says it was flying, and has a GPS time and position;
//   * it is not older than maxAgeS by GPS time (a record with no time, or a clock that went backwards, is refused);
//   * the position now is where an aircraft could have got to in that time (40 m/s, plus the last save interval and 1 km);
//   * the aircraft is moving at flying speed right now (minSpeedMs): a vario switched on at the launch, on the
//     ground, never resumes an earlier flight.
//
// Anything else discards the record. (App.cpp also gives up waiting after three minutes with no GPS time and fix.)
enum class ResumeVerdict : uint8_t { Resume, NotFlying, NoTime, Stale, Implausible, OnGround };

struct ResumeParams {
  uint32_t maxAgeS = 180 * 60;
  float minSpeedMs = 5.0f;
  float saveIntervalS = 60.0f;
  float maxSpeedMs = 40.0f;  // the fastest the aircraft could have moved since the last save
};

inline ResumeVerdict resumeDecide(const FlightRecord& rec, uint32_t nowUtc, double lat, double lon, float speedMs,
                                  const ResumeParams& p, uint32_t* ageS = nullptr) {
  if (!rec.flying) return ResumeVerdict::NotFlying;
  if (!rec.savedUtc || !nowUtc || nowUtc < rec.savedUtc) return ResumeVerdict::NoTime;
  const uint32_t age = nowUtc - rec.savedUtc;
  if (ageS) *ageS = age;
  if (age > p.maxAgeS) return ResumeVerdict::Stale;
  if (!rec.posValid) return ResumeVerdict::Implausible;
  const double reach = p.maxSpeedMs * (age + p.saveIntervalS) + 1000.0;
  if (geo::distanceM(rec.lastLat, rec.lastLon, lat, lon) > reach) return ResumeVerdict::Implausible;
  if (speedMs < p.minSpeedMs) return ResumeVerdict::OnGround;
  return ResumeVerdict::Resume;
}

inline const char* resumeVerdictName(ResumeVerdict v) {
  switch (v) {
    case ResumeVerdict::Resume: return "resume";
    case ResumeVerdict::NotFlying: return "the record was not flying";
    case ResumeVerdict::NoTime: return "no usable GPS time";
    case ResumeVerdict::Stale: return "the record is too old";
    case ResumeVerdict::Implausible: return "the position is not where the flight could have got to";
    case ResumeVerdict::OnGround: return "the aircraft is not moving at flying speed";
  }
  return "?";
}
