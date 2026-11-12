#pragma once
#include <math.h>
#include <stdint.h>

// Plausibility checks on the raw barometer samples, before they reach the vario filter. Pure logic, no
// hardware: Baro feeds it every sample (real or simulated) and the unit suite exercises it directly.
//
//   * Impossible pressure (outside 20000..110000 Pa, or not a number): rejected.
//   * Single wild sample: a sample further from the last accepted one than an aircraft can move (40 m/s
//     plus 3 m) is held back. If the NEXT sample agrees with it (within 3 m) the step is real (or at least
//     persistent) and is accepted as a Jump (the filter restarts there); if the next sample is back near
//     the old level the held-back one was a glitch and is dropped without a trace.
//   * Frozen sensor: the same value for stuckMs (a real sensor's last digits always move; 0 = check off).
//     Reported as Stuck for as long as the value stays frozen; the first different value after it is
//     reported as Recovered so that the filter restarts instead of carrying on from before.
class BaroGuard {
 public:
  enum class Verdict : uint8_t {
    Accept,     // use the sample
    Reject,     // impossible, or a lone outlier held back: ignore it
    Jump,       // a persistent step: use the sample, restart the filter there
    Stuck,      // frozen value: the barometer is not working
    Recovered,  // the first live sample after Stuck: use it, restart the filter
  };

  static constexpr float kMinPa = 20000.0f;
  static constexpr float kMaxPa = 110000.0f;
  static constexpr float kMaxRateMs = 40.0f;  // faster than this (between samples) is not an aircraft
  static constexpr float kJumpSlackM = 3.0f;
  static constexpr float kAgreeM = 3.0f;

  static bool pressureValid(float pa) { return pa >= kMinPa && pa <= kMaxPa; }  // false for NaN too

  void reset() { *this = BaroGuard(); }

  // altM: raw standard altitude; dtS: seconds since the previous sample (any); nowMs: clock; stuckMs: limit.
  Verdict check(float pa, float altM, float dtS, uint32_t nowMs, uint32_t stuckMs) {
    if (!pressureValid(pa) || !(fabsf(altM) < 30000.0f)) return Verdict::Reject;  // second test also catches NaN

    // frozen value
    if (haveLast_ && altM == lastRaw_) {
      if (!sameSinceMs_) sameSinceMs_ = nowMs;
    } else {
      sameSinceMs_ = 0;
    }
    lastRaw_ = altM;
    haveLast_ = true;
    if (stuckMs && sameSinceMs_ && nowMs - sameSinceMs_ >= stuckMs) {
      stuck_ = true;
      return Verdict::Stuck;
    }
    bool wasStuck = stuck_;
    stuck_ = false;

    if (!haveAccepted_) {
      accept(altM, nowMs);
      return wasStuck ? Verdict::Recovered : Verdict::Accept;
    }
    if (wasStuck) {  // live again: start from what it says now, nothing carried over
      accept(altM, nowMs);
      return Verdict::Recovered;
    }
    float since = (nowMs - lastAcceptMs_) * 1e-3f;
    if (since < dtS) since = dtS;
    if (since > 5.0f) since = 5.0f;
    if (fabsf(altM - lastAccepted_) > kMaxRateMs * since + kJumpSlackM) {
      if (pending_ && fabsf(altM - pendingAlt_) <= kAgreeM) {  // the next sample agrees: a real step
        pending_ = false;
        accept(altM, nowMs);
        return Verdict::Jump;
      }
      pending_ = true;
      pendingAlt_ = altM;
      return Verdict::Reject;
    }
    pending_ = false;
    accept(altM, nowMs);
    return Verdict::Accept;
  }

  bool stuck() const { return stuck_; }

 private:
  void accept(float altM, uint32_t nowMs) {
    lastAccepted_ = altM;
    lastAcceptMs_ = nowMs;
    haveAccepted_ = true;
  }

  bool haveLast_ = false, haveAccepted_ = false, pending_ = false, stuck_ = false;
  float lastRaw_ = 0, lastAccepted_ = 0, pendingAlt_ = 0;
  uint32_t lastAcceptMs_ = 0, sameSinceMs_ = 0;
};
