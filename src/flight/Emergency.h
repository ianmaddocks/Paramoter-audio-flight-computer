#pragma once
#include "nav/Nav.h"

class Wind;

// Engine-out guidance to the LZ: glide range, clock-face direction, on-course confirmation,
// periodic distance and off-course warnings.
class Emergency {
 public:
  bool active() const { return active_; }
  bool start(const NavState& nav, const Wind& wind);
  void cancel(bool announce);
  void update(const NavState& nav, const Wind& wind);

  float lastRangeM() const { return rangeM_; }

 private:
  bool active_ = false;
  bool onCourse_ = false;
  uint32_t lastUpdateMs_ = 0;
  uint32_t alignedSinceMs_ = 0;
  uint32_t offSinceMs_ = 0;
  uint32_t lastOffCallMs_ = 0;
  float rangeM_ = 0;
};
