#pragma once
#include <stdint.h>

// Whether there is a direction of travel to show: the compass arrow, the landing zone arrow and the windsock appear while it
// is true. A single threshold flickers when the ground speed hovers near it (a GPS standing still reports 1 to 2 m/s of
// noise), so there are two, and each change has to hold: it turns on once the speed has been at or above `showMs` for `holdMs`,
// and off once it has been below `hideMs` for `holdMs`. Between the two nothing changes. Losing the fix turns it off at once
// (without a position there is no direction to show). No hardware in it, so it is tested with scripted speeds.
class MovingState {
 public:
  void update(uint32_t nowMs, bool fix, float speedMs, float showMs, float hideMs, uint32_t holdMs) {
    if (hideMs > showMs) hideMs = showMs;
    if (!fix) {
      on_ = false;
      sinceMs_ = 0;
      return;
    }
    const bool change = on_ ? speedMs < hideMs : speedMs >= showMs;
    if (!change) {
      sinceMs_ = 0;
      return;
    }
    if (!sinceMs_) sinceMs_ = nowMs ? nowMs : 1;
    if (nowMs - sinceMs_ >= holdMs) {
      on_ = !on_;
      sinceMs_ = 0;
    }
  }
  bool on() const { return on_; }
  void reset() { on_ = false; sinceMs_ = 0; }

 private:
  bool on_ = false;
  uint32_t sinceMs_ = 0;  // when the speed first met the condition for changing, 0 = it does not now
};
