#pragma once
#include <stdint.h>

// The last ten minutes of height above ground, one sample every two seconds, for the rolling
// graph on the flight tab. Kept in RAM here (not in the page) so the graph is complete whenever
// a phone connects, and survives the page being reloaded or the phone sleeping.
class HeightHistory {
 public:
  static constexpr int kSamples = 300;
  static constexpr uint32_t kStepMs = 2000;

  // Call every flight-logic tick; samples are taken every kStepMs.
  void update(uint32_t nowMs, float aglM);

  // Copies the samples, oldest first, into out (room for kSamples). Returns how many. Each is
  // the height in quarter metres (value / 4 = metres).
  int copy(int16_t* out) const;

 private:
  int16_t v_[kSamples];
  int head_ = 0;  // next slot to write
  int count_ = 0;
  uint32_t lastMs_ = 0;
};
