#include "HeightHistory.h"

#include <Arduino.h>

void HeightHistory::push(int16_t v) {
  v_[head_] = v;
  head_ = (head_ + 1) % kSamples;
  if (count_ < kSamples) ++count_;
}

void HeightHistory::update(uint32_t nowMs, float aglM, bool flying) {
  if (!flying) {
    wasFlying_ = false;  // on the ground nothing is recorded, and what was is kept
    return;
  }
  if (!wasFlying_) {
    wasFlying_ = true;
    if (fresh_) {  // a new flight after one that was recorded: a gap between them
      for (int i = 0; i < kGapSamples; ++i) push(kGap);
      fresh_ = false;
    }
    lastMs_ = nowMs - kStepMs;  // the first sample at once
  }
  if (nowMs - lastMs_ < kStepMs) return;
  lastMs_ = nowMs;
  push((int16_t)constrain(lroundf(aglM * 4.0f), -32767L, 32767L));
  fresh_ = true;
}

int HeightHistory::copy(int16_t* out) const {
  for (int i = 0; i < count_; ++i) out[i] = v_[(head_ - count_ + i + kSamples) % kSamples];
  return count_;
}
