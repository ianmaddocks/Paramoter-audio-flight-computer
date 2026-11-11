#include "HeightHistory.h"

#include <Arduino.h>

void HeightHistory::update(uint32_t nowMs, float aglM) {
  if (count_ > 0 && nowMs - lastMs_ < kStepMs) return;
  lastMs_ = nowMs;
  v_[head_] = (int16_t)constrain(lroundf(aglM * 4.0f), -32768L, 32767L);
  head_ = (head_ + 1) % kSamples;
  if (count_ < kSamples) ++count_;
}

int HeightHistory::copy(int16_t* out) const {
  for (int i = 0; i < count_; ++i) out[i] = v_[(head_ - count_ + i + kSamples) % kSamples];
  return count_;
}
