#pragma once
#include <stdint.h>

// Plays a clip recorded at a lower sample rate through the fixed output rate. A voice clip needs only about 4 to 5 kHz of
// bandwidth, so clips are stored at a lower rate than the 16 kHz the speaker is driven at (a clip at 11 kHz is two thirds the
// size). Each output sample is a straight-line blend of the two clip samples either side of its place in time. No hardware in
// it, so it is tested with scripted samples (the "audio" unit suite). Clips at the output rate pass straight through.
class LinearResampler {
 public:
  // clipRate and outRate in Hz, clipSamples = samples in the clip. After this, outSamples() output samples are to be taken.
  void start(uint32_t clipRate, uint32_t outRate, uint32_t clipSamples) {
    step_ = clipRate >= outRate || clipRate == 0 ? 65536u : (uint32_t)(((uint64_t)clipRate << 16) / outRate);
    acc_ = 65536u;
    prev_ = cur_ = 0;
    srcLeft_ = clipSamples;
    out_ = step_ == 65536u ? clipSamples : (uint32_t)(((uint64_t)clipSamples * outRate + clipRate - 1) / clipRate);
  }
  uint32_t outSamples() const { return out_; }
  bool passThrough() const { return step_ == 65536u; }

  // The next output sample; decode() returns the next clip sample (it is only called while the clip has samples left).
  template <class F>
  int16_t next(F decode) {
    if (step_ == 65536u) return decode();
    while (acc_ >= 65536u) {
      acc_ -= 65536u;
      prev_ = cur_;
      if (srcLeft_ > 0) {
        --srcLeft_;
        cur_ = decode();
      }
    }
    const int32_t v = prev_ + (int32_t)(((int64_t)(cur_ - prev_) * (int64_t)acc_) >> 16);
    acc_ += step_;
    return (int16_t)v;
  }

 private:
  uint32_t step_ = 65536u, acc_ = 65536u, srcLeft_ = 0, out_ = 0;
  int32_t prev_ = 0, cur_ = 0;
};
