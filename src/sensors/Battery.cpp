#include "Battery.h"

#include <Arduino.h>

#include "Pins.h"
#include "Settings.h"
#include "sensors/Mic.h"
#include "test/Sim.h"

// mic::begin() must run first: while its ADC DMA is running it owns ADC1 and samples the
// battery pin too; analogRead is only used if the DMA sampler failed to start.
void Battery::begin() {
  if (!mic::running()) analogSetPinAttenuation(pins::BATT_ADC, ADC_11db);
  lastMs_ = 0;
  learner_.begin();
}

void Battery::update(uint32_t nowMs) {
  if (lastMs_ && nowMs - lastMs_ < 1000) return;
  lastMs_ = nowMs;
  if (sim::battOn()) {  // test build only; the learner is not fed simulated voltages
    volts_ = sim::battV();
    wasSim_ = true;
    return;
  }
  if (wasSim_) {  // back to the real battery: start from a fresh reading, not the simulated one
    wasSim_ = false;
    volts_ = 0;
  }
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
  volts_ = batteryFilter(volts_, batteryVoltsFromMv(mv));
  if (g_cfg.battLearn) learner_.update(volts_, nowMs);
}

float batteryVoltsFromMv(float mv) { return mv / 1000.0f * g_cfg.battDivider * g_cfg.battCal; }

float batteryFilter(float prev, float v) { return prev == 0 ? v : prev + (v - prev) * 0.2f; }

float batteryCurvePct(float volts) {
  // Top point is the user's "full" voltage (100%); below 4.00 V it follows a typical LiPo curve.
  const float kCurve[][2] = {{max(g_cfg.battFullV, 4.01f), 100}, {4.00f, 80}, {3.90f, 65}, {3.80f, 50},
                             {3.75f, 40}, {3.70f, 30}, {3.65f, 20}, {3.60f, 12},
                             {3.50f, 5},  {3.30f, 0}};
  const int n = sizeof(kCurve) / sizeof(kCurve[0]);
  if (volts >= kCurve[0][0]) return 100;  // never above 100%
  for (int i = 1; i < n; ++i) {
    if (volts >= kCurve[i][0]) {
      float t = (volts - kCurve[i][0]) / (kCurve[i - 1][0] - kCurve[i][0]);
      return kCurve[i][1] + t * (kCurve[i - 1][1] - kCurve[i][1]);
    }
  }
  return 0;
}

int Battery::percent() const {
  if (g_cfg.battLearn && learner_.learned()) {
    float full = learner_.minutesAt(g_cfg.battFullV);
    if (full > 1.0f) return (int)constrain(lroundf(100.0f * learner_.minutesAt(volts_) / full), 0L, 100L);
  }
  return (int)lroundf(batteryCurvePct(volts_));
}

int Battery::minutesLeftOrUnknown() const { return g_cfg.battLearn ? learner_.minutesLeft(volts_) : -1; }
