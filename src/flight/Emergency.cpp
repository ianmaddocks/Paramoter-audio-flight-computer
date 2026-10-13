#include "Emergency.h"

#include <Arduino.h>

#include "Log.h"
#include "Settings.h"
#include "audio/Speech.h"
#include "flight/Wind.h"
#include "nav/Geo.h"

using audio::Priority;
using audio::Utterance;

namespace {
constexpr float kMinGuidanceSpeed = 3.0f;
constexpr uint32_t kAlignHoldMs = 2000;
constexpr uint32_t kOffHoldMs = 3000;
constexpr uint32_t kOffRepeatMs = 8000;
}  // namespace

bool Emergency::start(const NavState& nav, const Wind& wind) {
  if (!nav.fix || !nav.lzValid) {
    speech::say(Priority::Urgent, !nav.fix ? "no_gps" : "no_landing_zone");
    return false;
  }
  active_ = true;
  onCourse_ = false;
  alignedSinceMs_ = offSinceMs_ = 0;
  lastOffCallMs_ = lastUpdateMs_ = nav.ms;

  float dist = geo::distanceM(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
  float brg = geo::bearingDeg(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
  rangeM_ = fmaxf(nav.aglM, 0.0f) * wind.groundGlide(brg);
  LOGI("EMG", "start: agl %.0f m, LZ %.0f m at %.0f deg (rel %.0f), range %.0f m, ground glide %.2f",
       nav.aglM, dist, brg, geo::wrap180(brg - nav.trackDeg), rangeM_, wind.groundGlide(brg));

  Utterance u(Priority::Urgent);
  u.add("emergency").add("glide_range");
  speech::addDistance(u, rangeM_);
  u.add("landing_zone");
  if (nav.speedMs >= kMinGuidanceSpeed) speech::addClock(u, brg - nav.trackDeg);
  speech::addDistance(u, dist);
  if (dist > rangeM_) u.add("cannot_reach");
  audio::say(u);
  return true;
}

void Emergency::cancel(bool announce) {
  if (!active_) return;
  active_ = false;
  if (announce) speech::say(Priority::Urgent, "emergency_off");
}

void Emergency::update(const NavState& nav, const Wind& wind) {
  if (!active_ || !nav.fix) return;
  const uint32_t now = nav.ms;
  float dist = geo::distanceM(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
  float brg = geo::bearingDeg(nav.lat, nav.lon, nav.lzLat, nav.lzLon);
  rangeM_ = fmaxf(nav.aglM, 0.0f) * wind.groundGlide(brg);

  if (nav.speedMs >= kMinGuidanceSpeed) {
    float rel = geo::wrap180(brg - nav.trackDeg);
    if (fabsf(rel) <= g_cfg.onCourseDeg) {
      offSinceMs_ = 0;
      if (!alignedSinceMs_) alignedSinceMs_ = now;
      if (!onCourse_ && now - alignedSinceMs_ >= kAlignHoldMs) {
        onCourse_ = true;
        speech::say(Priority::High, "on_course");
      }
    } else {
      alignedSinceMs_ = 0;
      if (fabsf(rel) > g_cfg.offHeadingDeg) {
        if (!offSinceMs_) offSinceMs_ = now;
        if (now - offSinceMs_ >= kOffHoldMs && now - lastOffCallMs_ >= kOffRepeatMs) {
          lastOffCallMs_ = now;
          onCourse_ = false;
          Utterance u(Priority::High);
          u.add("landing_zone");
          speech::addClock(u, rel);
          audio::say(u);
        }
      } else {
        offSinceMs_ = 0;
      }
    }
  }

  if (now - lastUpdateMs_ >= (uint32_t)g_cfg.emergencyUpdateS * 1000) {
    lastUpdateMs_ = now;
    LOGD("EMG", "dist %.0f m brg %.0f trk %.0f range %.0f m agl %.0f m", dist, brg, nav.trackDeg, rangeM_,
         nav.aglM);
    Utterance u(Priority::Normal);
    u.add("distance");
    speech::addDistance(u, dist);
    if (dist > rangeM_) u.add("cannot_reach");
    audio::say(u);
  }
}
