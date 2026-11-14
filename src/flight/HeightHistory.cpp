#include "HeightHistory.h"

#include <Arduino.h>
#include <LittleFS.h>

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

namespace {
constexpr uint32_t kMagic = 0x31544748;  // "HGT1"
}

bool HeightHistory::save(const char* path) const {
  File f = LittleFS.open(path, "w");
  if (!f) return false;
  int16_t out[kSamples];
  const uint16_t n = (uint16_t)copy(out);
  f.write((const uint8_t*)&kMagic, 4);
  f.write((const uint8_t*)&n, 2);
  f.write((const uint8_t*)out, 2 * n);
  f.close();
  return true;
}

bool HeightHistory::load(const char* path) {
  File f = LittleFS.open(path, "r");
  if (!f) return false;
  uint32_t magic = 0;
  uint16_t n = 0;
  if (f.read((uint8_t*)&magic, 4) != 4 || magic != kMagic || f.read((uint8_t*)&n, 2) != 2 || n > kSamples) return false;
  if (n && f.read((uint8_t*)v_, 2 * n) != (int)(2 * n)) return false;
  count_ = n;
  head_ = n % kSamples;
  fresh_ = n > 0;  // the next flight starts after a gap
  wasFlying_ = false;
  return true;
}
