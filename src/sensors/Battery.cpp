#include "Battery.h"

#include <Arduino.h>

#include "Pins.h"
#include "Settings.h"
#include "sensors/Mic.h"

// mic::begin() must run first: while its ADC DMA is running it owns ADC1 and samples the
// battery pin too; analogRead is only used if the DMA sampler failed to start.
void Battery::begin() {
  if (!mic::running()) analogSetPinAttenuation(pins::BATT_ADC, ADC_11db);
  lastMs_ = 0;
}

void Battery::update(uint32_t nowMs) {
  if (lastMs_ && nowMs - lastMs_ < 1000) return;
  lastMs_ = nowMs;
  float mv;
  if (mic::running()) {
    int32_t m = mic::batteryMilliVolts();
    if (m < 0) return;
    mv = (float)m;
  } else {
    uint32_t sum = 0;
    for (int i = 0; i < 16; ++i) sum += analogReadMilliVolts(pins::BATT_ADC);
    mv = sum / 16.0f;
  }
  float v = mv / 1000.0f * g_cfg.battDivider * g_cfg.battCal;
  volts_ = volts_ == 0 ? v : volts_ + (v - volts_) * 0.2f;
}

int Battery::percent() const {
  static const float kCurve[][2] = {{4.20f, 100}, {4.10f, 90}, {4.00f, 80}, {3.90f, 65},
                                    {3.80f, 50},  {3.75f, 40}, {3.70f, 30}, {3.65f, 20},
                                    {3.60f, 12},  {3.50f, 5},  {3.30f, 0}};
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (volts_ >= kCurve[0][0]) return 100;
  for (int i = 1; i < n; ++i) {
    if (volts_ >= kCurve[i][0]) {
      float t = (volts_ - kCurve[i][0]) / (kCurve[i - 1][0] - kCurve[i][0]);
      return (int)lroundf(kCurve[i][1] + t * (kCurve[i - 1][1] - kCurve[i][1]));
    }
  }
  return 0;
}
