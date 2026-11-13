#pragma once
#include <stdint.h>

// The height above ground while flying, one sample every two seconds, for the rolling graph on the flight tab: the last
// ten minutes flown (300 samples). Nothing is recorded on the ground, and the trace is kept after landing (it is not
// cleared). A new flight continues it after a gap of kGapSamples marker samples, so separate flights show as separate
// traces. Kept in RAM here (not in the page) so the graph is complete whenever a phone connects, and survives the page
// being reloaded or the phone sleeping.
class HeightHistory {
 public:
  static constexpr int kSamples = 300;
  static constexpr uint32_t kStepMs = 2000;
  static constexpr int16_t kGap = INT16_MIN;  // a sample that is not a height: the break between two flights
  static constexpr int kGapSamples = 15;      // 30 s wide

  // Call every flight-logic tick; samples are taken every kStepMs while flying, none on the ground.
  void update(uint32_t nowMs, float aglM, bool flying);

  // Copies the samples, oldest first, into out (room for kSamples). Returns how many. Each is the height in quarter
  // metres (value / 4 = metres), or kGap.
  int copy(int16_t* out) const;

 private:
  void push(int16_t v);
  int16_t v_[kSamples];
  int head_ = 0;  // next slot to write
  int count_ = 0;
  uint32_t lastMs_ = 0;
  bool wasFlying_ = false;
  bool fresh_ = false;  // heights recorded since the last gap
};
