#include "Glide.h"

#include <Arduino.h>

#include "Settings.h"

namespace {
constexpr float kMinSinkMs = 0.15f;  // less average descent than this is level flight: no ratio
constexpr float kMaxRatio = 50.0f;   // reported as at most this (the page shows ">50")
}  // namespace

void Glide::update(uint32_t nowMs, const NavState& nav) {
  if (!nav.flying) {  // nothing carries over from one flight to the next
    count_ = head_ = 0;
    sumSpeed_ = sumSink_ = 0;
    sumN_ = 0;
    started_ = false;
    return;
  }
  if (!started_) {
    started_ = true;
    binStartMs_ = nowMs;
  }
  if (nav.fix && nav.speedOk) {
    sumSpeed_ += nav.speedMs;
    sumSink_ += -nav.varioMs;
    ++sumN_;
  }
  if (nowMs - binStartMs_ >= 1000) {
    binStartMs_ = nowMs;
    if (sumN_ > 0) {
      bins_[head_] = {nowMs, sumSpeed_ / sumN_, sumSink_ / sumN_};
      head_ = (head_ + 1) % kMaxBins;
      if (count_ < kMaxBins) ++count_;
    }
    sumSpeed_ = sumSink_ = 0;
    sumN_ = 0;
  }
}

float Glide::ratioOver(uint32_t nowMs, int seconds, int minBins) const {
  float speed = 0, sink = 0;
  int n = 0;
  for (int i = 0; i < count_; ++i) {  // newest first
    const Bin& b = bins_[(head_ - 1 - i + kMaxBins) % kMaxBins];
    if (nowMs - b.endMs > (uint32_t)(seconds + 1) * 1000) break;
    speed += b.speedMs;
    sink += b.sinkMs;
    ++n;
  }
  if (n < minBins) return NAN;
  speed /= n;
  sink /= n;
  if (sink < kMinSinkMs) return NAN;
  return fminf(speed / sink, kMaxRatio);
}

float Glide::shortRatio(uint32_t nowMs) const { return ratioOver(nowMs, g_cfg.glideShortS, 1); }

float Glide::longRatio(uint32_t nowMs) const {
  return ratioOver(nowMs, g_cfg.glideLongS, max(3, (int)g_cfg.glideLongS / 4));
}
