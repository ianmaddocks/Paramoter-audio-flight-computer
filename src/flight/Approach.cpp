#include "Approach.h"
#include "Log.h"

#include <Arduino.h>

#include "Settings.h"
#include "audio/Speech.h"
#include "flight/Wind.h"
#include "nav/Geo.h"

using audio::Priority;
using audio::Utterance;

namespace {
constexpr float kLookaheadM = 150.0f;
constexpr float kMinGuidanceSpeed = 3.0f;
constexpr uint32_t kOffHeadingHoldMs = 4000;
constexpr uint32_t kSteerRepeatMs = 10000;
constexpr uint32_t kGlideCallRepeatMs = 15000;
constexpr float kSideDetectDeg = 45.0f;
constexpr float kSideDetectM = 40.0f;
}  // namespace

const char* Approach::phaseName() const { return nameOf(phase_); }

const char* Approach::nameOf(ApPhase p) {
  switch (p) {
    case ApPhase::ToStart: return "to start";
    case ApPhase::Upwind: return "upwind";
    case ApPhase::Crosswind: return "crosswind";
    case ApPhase::Downwind: return "downwind";
    case ApPhase::Base: return "base";
    case ApPhase::Final: return "final";
    default: return "off";
  }
}

Approach::XY Approach::toFrame(const NavState& nav) const {
  geo::EN p = geo::toLocal(nav.lzLat, nav.lzLon, nav.lat, nav.lon);
  float u = upwindDeg_ * geo::kDegToRadF;
  return {p.e * sinf(u) + p.n * cosf(u), p.e * cosf(u) - p.n * sinf(u)};
}

float Approach::bearingOf(float dx, float dy) const {
  return geo::wrap360(upwindDeg_ + atan2f(dy, dx) * geo::kRadToDegF);
}

float Approach::headingOf(float relDeg) const { return geo::wrap360(upwindDeg_ + relDeg); }

float Approach::lead(const NavState& nav) const {
  return fmaxf(nav.speedMs, 5.0f) * g_cfg.turnLeadS;
}

void Approach::setPhase(ApPhase p, uint32_t now) {
  LOGD("APR", "%s -> %s at x=%.0f y=%.0f agl=%.0f side=%d", phaseName(), nameOf(p), tracePos_.x,
       tracePos_.y, traceAgl_, side_);
  phase_ = p;
  phaseSinceMs_ = now;
  lastUpdateMs_ = now;
  offSinceMs_ = 0;
}

void Approach::turnCall(bool right) {
  Utterance u(Priority::Urgent);
  speech::addTurn(u, right);
  audio::say(u);
}

void Approach::steerTo(const NavState& nav, float desiredDeg) {
  if (nav.speedMs < kMinGuidanceSpeed) {
    offSinceMs_ = 0;
    return;
  }
  float diff = geo::wrap180(desiredDeg - nav.trackDeg);
  if (fabsf(diff) <= g_cfg.offHeadingDeg) {
    offSinceMs_ = 0;
    return;
  }
  if (!offSinceMs_) {
    offSinceMs_ = nav.ms;
    return;
  }
  if (nav.ms - offSinceMs_ >= kOffHeadingHoldMs && nav.ms - lastSteerMs_ >= kSteerRepeatMs) {
    lastSteerMs_ = nav.ms;
    speech::say(Priority::High, diff > 0 ? "steer_right" : "steer_left");
  }
}

// Steers along the leg line through (px, py) at relDeg from upwind, converging on the line.
void Approach::followLeg(const NavState& nav, XY pos, float px, float py, float relDeg) {
  float a = relDeg * geo::kDegToRadF;
  float rightOfLine = -(pos.x - px) * sinf(a) + (pos.y - py) * cosf(a);
  steerTo(nav, headingOf(relDeg) - atan2f(rightOfLine, kLookaheadM) * geo::kRadToDegF);
}

void Approach::callStartPoint(const NavState& nav, XY pos, bool first) {
  float dx = xStart_ - pos.x, dy = -pos.y;
  Utterance u(first ? Priority::Urgent : Priority::Normal);
  if (first) u.add("approach_on");
  u.add("circuit_start");
  if (nav.speedMs >= kMinGuidanceSpeed) speech::addClock(u, bearingOf(dx, dy) - nav.trackDeg);
  speech::addDistance(u, hypotf(dx, dy));
  audio::say(u);
  lastUpdateMs_ = nav.ms;
}

bool Approach::start(const NavState& nav, const Wind& wind) {
  const char* problem = !nav.fix        ? "no_gps"
                        : !nav.lzValid  ? "no_landing_zone"
                        : !wind.valid() ? "wind_unknown"
                                        : nullptr;
  if (problem) {
    speech::say(Priority::Urgent, problem);
    return false;
  }
  upwindDeg_ = wind.fromDeg();
  side_ = g_cfg.circuitSide == CS_LEFT ? -1 : g_cfg.circuitSide == CS_RIGHT ? 1 : 0;
  float finalLen = fmaxf(g_cfg.finalHeightM * wind.groundGlide(upwindDeg_), g_cfg.minFinalM);
  xBaseNominal_ = xBase_ = -finalLen;
  xCross_ = xBase_ + g_cfg.downwindLegM;
  xStart_ = xCross_ - g_cfg.upwindLegM;
  descendCalled_ = tooLowCalled_ = false;
  lastSteerMs_ = lastGlideCallMs_ = 0;
  setPhase(ApPhase::ToStart, nav.ms);
  callStartPoint(nav, toFrame(nav), true);
  LOGI("APR", "Approach: upwind %.0f, start x=%.0f, crosswind x=%.0f, nominal base x=%.0f", upwindDeg_,
        xStart_, xCross_, xBase_);
  return true;
}

void Approach::cancel(bool announce) {
  if (phase_ == ApPhase::Off) return;
  phase_ = ApPhase::Off;
  if (announce) speech::say(Priority::Urgent, "approach_off");
}

void Approach::update(const NavState& nav, const Wind& wind) {
  if (phase_ == ApPhase::Off || !nav.fix) return;
  const uint32_t now = nav.ms;
  const XY pos = toFrame(nav);
  const float S = g_cfg.crosswindLegM;
  const float ld = lead(nav);
  tracePos_ = pos;
  traceAgl_ = nav.aglM;

  switch (phase_) {
    case ApPhase::ToStart: {
      float dx = xStart_ - pos.x, dy = -pos.y;
      bool arrived = hypotf(dx, dy) < g_cfg.arrivalRadiusM ||
                     (pos.x > xStart_ && pos.x < xCross_ - ld && fabsf(pos.y) < g_cfg.arrivalRadiusM);
      if (arrived) {
        setPhase(ApPhase::Upwind, now);
        float diff = geo::wrap180(upwindDeg_ - nav.trackDeg);
        if (fabsf(diff) > g_cfg.offHeadingDeg) turnCall(diff > 0);
        break;
      }
      if (!descendCalled_ && nav.aglM > g_cfg.circuitHeightM + 30.0f) {
        descendCalled_ = true;
        Utterance u(Priority::Normal);
        u.add("descend_to");
        speech::addHeight(u, g_cfg.circuitHeightM);
        audio::say(u);
      }
      float rel = geo::wrap180(bearingOf(dx, dy) - nav.trackDeg);
      bool off = nav.speedMs >= kMinGuidanceSpeed && fabsf(rel) > g_cfg.offHeadingDeg;
      if (!off) offSinceMs_ = 0;
      else if (!offSinceMs_) offSinceMs_ = now;
      bool offDue = off && now - offSinceMs_ >= kOffHeadingHoldMs && now - lastSteerMs_ >= kSteerRepeatMs;
      if (offDue) lastSteerMs_ = now;
      if (offDue || now - lastUpdateMs_ >= (uint32_t)g_cfg.guidanceUpdateS * 1000) {
        callStartPoint(nav, pos, false);
      }
      break;
    }

    case ApPhase::Upwind:
      followLeg(nav, pos, xStart_, 0, 0);
      if (pos.x >= xCross_ - ld) {
        setPhase(ApPhase::Crosswind, now);
        if (side_) turnCall(side_ > 0);
        else speech::say(Priority::Urgent, "turn_crosswind_now");
      }
      break;

    case ApPhase::Crosswind:
      if (!side_) {  // pilot's choice: take the side from the direction they turned
        float d = geo::wrap180(nav.trackDeg - upwindDeg_);
        if (fabsf(d) > kSideDetectDeg) side_ = d > 0 ? 1 : -1;
        else if (fabsf(pos.y) > kSideDetectM) side_ = pos.y > 0 ? 1 : -1;
        if (!side_) break;
        LOGI("APR", "Approach: %s-hand circuit", side_ > 0 ? "right" : "left");
      }
      followLeg(nav, pos, xCross_, 0, 90.0f * side_);
      if (side_ * pos.y >= S - ld) {
        setPhase(ApPhase::Downwind, now);
        turnCall(side_ > 0);
      }
      break;

    case ApPhase::Downwind: {
      followLeg(nav, pos, xCross_, side_ * S, 180.0f);
      // Base turn point from current height: glide S along base, then final to the LZ.
      float hAtFinal = nav.aglM - S / wind.groundGlide(headingOf(-90.0f * side_));
      float xb = hAtFinal * wind.groundGlide(upwindDeg_);
      float maxXb = -xBaseNominal_ + g_cfg.downwindLegM;
      if (xb < g_cfg.minFinalM) {
        xb = g_cfg.minFinalM;
        if (!tooLowCalled_) {
          tooLowCalled_ = true;
          speech::say(Priority::High, "too_low");
        }
      } else if (xb > maxXb) {
        xb = maxXb;
        if (now - lastGlideCallMs_ >= kGlideCallRepeatMs * 2) {
          lastGlideCallMs_ = now;
          speech::say(Priority::Normal, "too_high");
        }
      }
      if (now - lastTraceMs_ >= 2000) {
        lastTraceMs_ = now;
        LOGD("APR", "downwind x=%.0f agl=%.0f hAtFinal=%.0f baseTurnX=%.0f lead=%.0f", pos.x, nav.aglM,
             hAtFinal, -xb, ld);
      }
      if (pos.x <= -xb + ld) {
        xBase_ = pos.x;
        setPhase(ApPhase::Base, now);
        turnCall(side_ > 0);
      }
      break;
    }

    case ApPhase::Base: {
      followLeg(nav, pos, xBase_, side_ * S, -90.0f * side_);
      float hNeed = hypotf(pos.x, pos.y) / wind.groundGlide(bearingOf(-pos.x, -pos.y));
      if (side_ * pos.y <= ld || nav.aglM < hNeed * 1.05f) {
        setPhase(ApPhase::Final, now);
        turnCall(side_ > 0);
      }
      break;
    }

    case ApPhase::Final: {
      float dist = hypotf(pos.x, pos.y);
      steerTo(nav, bearingOf(-pos.x, -pos.y));
      if (pos.x > 30.0f || nav.aglM < 3.0f) {
        phase_ = ApPhase::Off;
        break;
      }
      float hNeed = dist / wind.groundGlide(upwindDeg_);
      if (now - lastGlideCallMs_ >= kGlideCallRepeatMs) {
        if (nav.aglM > hNeed + fmaxf(15.0f, 0.3f * hNeed)) {
          lastGlideCallMs_ = now;
          speech::say(Priority::High, "too_high");
        } else if (nav.aglM < hNeed * 0.8f - 5.0f) {
          lastGlideCallMs_ = now;
          speech::say(Priority::High, "too_low");
        }
      }
      break;
    }

    default: break;
  }
}
