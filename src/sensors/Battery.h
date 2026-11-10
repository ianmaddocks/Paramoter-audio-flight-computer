#pragma once
#include <stdint.h>

#include "sensors/BatteryLearn.h"

// Percentage for a battery voltage from the built-in typical LiPo curve (100% at the
// "voltage shown as 100%" setting, never above 100).
float batteryCurvePct(float volts);

class Battery {
 public:
  void begin();
  void update(uint32_t nowMs);
  float volts() const { return volts_; }
  // Follows the learned discharge once one has been measured, else the typical curve.
  int percent() const;
  // Estimated minutes of battery left, -1 when unknown (charging, or not on battery).
  int minutesLeftOrUnknown() const;

 private:
  float volts_ = 0;
  uint32_t lastMs_ = 0;
  BatteryLearner learner_;
};
