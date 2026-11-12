#pragma once
#include <stdint.h>

#include "sensors/BatteryLearn.h"

// Percentage for a battery voltage from the built-in typical LiPo curve (100% at the
// "voltage shown as 100%" setting, never above 100).
float batteryCurvePct(float volts);

// ADC pin millivolts to battery volts through the divider and the calibration setting.
float batteryVoltsFromMv(float mv);
// One step of the smoothing of the reading (the first reading, prev = 0, is taken as it is).
float batteryFilter(float prev, float reading);

class Battery {
 public:
  void begin();
  void update(uint32_t nowMs);
  float volts() const { return volts_; }
  // Follows the learned discharge once one has been measured, else the typical curve.
  int percent() const;
  // Estimated minutes of battery left, -1 when unknown (charging, or not on battery).
  int minutesLeftOrUnknown() const;
  bool learned() const { return learner_.learned(); }

 private:
  float volts_ = 0;
  uint32_t lastMs_ = 0;
  bool wasSim_ = false;
  BatteryLearner learner_;
};
