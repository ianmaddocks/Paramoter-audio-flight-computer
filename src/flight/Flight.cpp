#include "Flight.h"

#include <Arduino.h>

#include "Log.h"
#include "Settings.h"
#include "nav/Geo.h"
#include "sensors/Baro.h"
#include "sensors/GpsData.h"

namespace {
constexpr float kStillSpeedMs = 2.0f;
constexpr float kOffsetTauS = 120.0f;
constexpr float kGroundTauS = 5.0f;
constexpr int kMinAltSats = 6;
constexpr float kMinGpsAltM = -500.0f;
constexpr float kMaxGpsAltM = 9000.0f;
constexpr float kMaxDatumJumpM = 200.0f;  // GPS altitude this far from the baro datum is rejected
}  // namespace

FlightEvent Flight::update(uint32_t now, const Baro& baro, const GpsData& gps, NavState& n) {
  FlightEvent ev = FlightEvent::None;
  float dt = lastMs_ ? (now - lastMs_) / 1000.0f : 0.1f;
  lastMs_ = now;

  n.ms = now;
  n.fix = gps.fix;
  n.sats = gps.sats;
  n.hdop = gps.hdop;
  n.speedOk = false;
  if (n.fix) {
    n.lat = gps.lat;
    n.lon = gps.lon;
    // A weak fix (few satellites, poor accuracy) reports speed noise of tens of km/h while
    // stationary: only trust speed and course from a decent fix.
    bool speedOk = n.sats >= kMinAltSats && n.hdop < 3.0f;
    n.speedOk = speedOk;
    n.speedMs = speedOk ? gps.speedMs : 0.0f;
    if (speedOk) n.trackDeg = gps.courseDeg;
  } else {
    n.speedMs = 0;
  }
  n.gpsGood = n.fix && n.sats >= g_cfg.readyMinSats && n.hdop <= g_cfg.readyMaxHdop && gps.steady;
  // A weak fix (indoors, few satellites) can report absurd altitudes (tens of km): only trust GPS
  // altitude from a decent fix, within a believable range, and, once the baro datum is set,
  // close to what the baro implies.
  bool gpsAltOk = n.fix && gps.altValid && n.hdop < 3.0f && n.sats >= kMinAltSats &&
                  gps.altM > kMinGpsAltM && gps.altM < kMaxGpsAltM;
  if (gpsAltOk && offsetInit_ && n.baroOk && fabsf(gps.altM - n.altStdM - offset_) > kMaxDatumJumpM) gpsAltOk = false;
  if (gpsAltOk) n.gpsAltM = gps.altM;

  n.baroOk = baro.ok();
  n.pressurePa = baro.pressurePa();
  n.tempC = baro.tempC();
  n.altStdM = baro.altStdM();
  n.varioMs = n.baroOk && baro.settled() ? baro.varioMs() : 0.0f;

  // Baro gives smooth relative altitude; GPS slowly removes its drift and sets the MSL datum.
  if (n.baroOk && gpsAltOk) {
    float target = n.gpsAltM - n.altStdM;
    if (!offsetInit_) {
      float delta = target - offset_;
      offset_ = target;
      offsetInit_ = true;
      if (groundValid_) groundAlt_ += delta;
      LOGI("FLT", "GPS altitude datum set: baro offset %.1f m", offset_);
    } else {
      offset_ += (target - offset_) * fminf(1.0f, dt / kOffsetTauS);
    }
  }
  float alt = n.baroOk ? n.altStdM + offset_ : (gpsAltOk ? n.gpsAltM : lastAlt_);
  lastAlt_ = alt;
  n.altMslM = alt;

  bool still = !n.fix || n.speedMs < kStillSpeedMs;
  // The ground reference follows slow pressure drift but must not follow the aircraft. GPS speed
  // says when it is moving; without that (no GPS, or a launch into wind at walking pace) a steady
  // climb or sink does. Otherwise the reference trails a climb by only a few metres and a
  // take-off is never seen, and height above ground reads low afterwards.
  if (n.baroOk && fabsf(n.varioMs) >= g_cfg.groundHoldVarioMs) still = false;
  if (!flying_) {
    if (still && (n.baroOk || gpsAltOk)) {
      if (!groundValid_) {
        groundAlt_ = alt;
        groundValid_ = true;
      } else {
        groundAlt_ += (alt - groundAlt_) * fminf(1.0f, dt / kGroundTauS);
      }
    }
    if (still && n.gpsGood) {
      autoLzLat_ = n.lat;
      autoLzLon_ = n.lon;
      autoLzValid_ = true;
    }
    if (still && n.baroOk) {
      if (!groundStdValid_) {
        groundStd_ = n.altStdM;
        groundStdValid_ = true;
      } else {
        groundStd_ += (n.altStdM - groundStd_) * fminf(1.0f, dt / kGroundTauS);
      }
    }

    bool fast = n.gpsGood && n.speedMs >= g_cfg.takeoffSpeedMs;
    if (fast) {
      if (!fastSinceMs_) {
        fastSinceMs_ = now;
        courseE_ = courseN_ = 0;
        LOGD("FLT", "takeoff speed reached (%.1f m/s), waiting for height gain", n.speedMs);
      }
      courseE_ += sinf(n.trackDeg * geo::kDegToRadF);
      courseN_ += cosf(n.trackDeg * geo::kDegToRadF);
    } else {
      if (fastSinceMs_) LOGD("FLT", "takeoff candidate ended after %lu ms", (unsigned long)(now - fastSinceMs_));
      fastSinceMs_ = 0;
    }
    // Pressure altitude only: a bad GPS altitude must not be able to fake a height gain.
    float gain = groundStdValid_ && n.baroOk ? n.altStdM - groundStd_ : groundValid_ ? alt - groundAlt_ : 0.0f;
    bool gpsTakeoff = fastSinceMs_ && now - fastSinceMs_ >= (uint32_t)g_cfg.takeoffTimeS * 1000 &&
                      gain >= g_cfg.takeoffGainM;
    bool baroOnlyTakeoff = !n.gpsGood && n.baroOk && gain >= g_cfg.baroTakeoffGainM;
    if (gpsTakeoff || baroOnlyTakeoff) {
      flying_ = true;
      takeoffMs_ = gpsTakeoff ? fastSinceMs_ : now;
      takeoffCourseValid_ = gpsTakeoff;
      takeoffCourseDeg_ = geo::vecBearing(courseE_, courseN_);
      stillSinceMs_ = 0;
      ev = FlightEvent::Takeoff;
      LOGI("FLT", "takeoff (%s): gain %.1f m, ground %.1f m MSL, LZ %.6f,%.6f",
           gpsTakeoff ? "gps" : "baro only", gain, groundAlt_, autoLzLat_, autoLzLon_);
    }
  }

  if (g_cfg.lzMode == LZ_MANUAL) {
    n.lzValid = g_cfg.lzLat != 0.0 || g_cfg.lzLon != 0.0;
    n.lzLat = g_cfg.lzLat;
    n.lzLon = g_cfg.lzLon;
    n.lzElevM = g_cfg.lzElevM;
  } else {
    n.lzValid = autoLzValid_;
    n.lzLat = autoLzLat_;
    n.lzLon = autoLzLon_;
    n.lzElevM = groundAlt_;
  }
  n.aglM = (g_cfg.lzMode == LZ_MANUAL || groundValid_) ? alt - n.lzElevM : 0.0f;

  if (flying_ && ev == FlightEvent::None) {
    bool slow = n.fix ? n.speedMs < g_cfg.landingSpeedMs : fabsf(n.varioMs) < 0.3f;
    uint32_t needMs = (uint32_t)g_cfg.landingTimeS * 1000 * (n.fix ? 1 : 2);
    if (slow) {
      if (!stillSinceMs_) {
        stillSinceMs_ = now;
        LOGD("FLT", "landing candidate: gs %.1f m/s, agl %.1f m", n.speedMs, n.aglM);
      }
      if (now - stillSinceMs_ >= needMs) {
        flying_ = false;
        fastSinceMs_ = 0;
        groundValid_ = false;
        groundStdValid_ = false;
        ev = FlightEvent::Landing;
      }
    } else {
      stillSinceMs_ = 0;
    }
  }

  if (flying_) lastFlightTimeS_ = (now - takeoffMs_) / 1000;
  n.flying = flying_;
  n.flightTimeS = lastFlightTimeS_;
  if (resumePending_ && ev == FlightEvent::None) {
    resumePending_ = false;
    ev = FlightEvent::Resumed;
  }
  return ev;
}

void Flight::exportState(FlightRecord& r) const {
  r.flying = flying_;
  r.flightTimeS = lastFlightTimeS_;
  r.lzValid = autoLzValid_;
  r.lzLat = autoLzLat_;
  r.lzLon = autoLzLon_;
  r.groundValid = groundValid_;
  r.groundAltM = groundAlt_;
  r.offsetInit = offsetInit_;
  r.offsetM = offset_;
  r.takeoffCourseValid = takeoffCourseValid_;
  r.takeoffCourseDeg = takeoffCourseDeg_;
}

void Flight::importState(const FlightRecord& r, uint32_t now) {
  autoLzValid_ = r.lzValid;
  autoLzLat_ = r.lzLat;
  autoLzLon_ = r.lzLon;
  if (!r.flying) return;
  flying_ = true;
  groundValid_ = r.groundValid;
  groundAlt_ = r.groundAltM;
  offset_ = r.offsetM;
  offsetInit_ = r.offsetInit;
  takeoffMs_ = now - r.flightTimeS * 1000;
  lastFlightTimeS_ = r.flightTimeS;
  takeoffCourseValid_ = r.takeoffCourseValid;
  takeoffCourseDeg_ = r.takeoffCourseDeg;
  stillSinceMs_ = 0;
  resumePending_ = true;
}

void Flight::abandonFlight() {
  flying_ = false;
  groundValid_ = false;
  groundStdValid_ = false;
  fastSinceMs_ = 0;
  stillSinceMs_ = 0;
  resumePending_ = false;
}
