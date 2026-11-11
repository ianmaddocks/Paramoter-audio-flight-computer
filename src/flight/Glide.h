#pragma once
#include <math.h>
#include <stdint.h>

#include "nav/Nav.h"

// Glide ratio actually being achieved over the ground: ground distance covered per unit of
// height lost (7.0 = 7 m forward for every 1 m of descent), for the Glide ratio tile.
//
// Ground speed (GPS) and sink rate (barometer vario) are each averaged over a window and then
// divided; averaging the ratio itself would be dominated by moments when the sink is close to
// zero. Two windows are reported, the short one (default 1 s) as the main value and the long
// one (default 60 s) as the steadier figure; both are set in the Glide settings.
//
// Only in flight with a trustworthy GPS speed does anything count. A window with no net descent
// (level or climbing) has no glide ratio and reports NAN, as does one with too little data.
// A long window that includes a climb gives the overall figure: distance covered per height lost
// net of the climb.
class Glide {
 public:
  static constexpr int kMaxBins = 120;  // one per second: the longest window

  // Call every flight-logic tick.
  void update(uint32_t nowMs, const NavState& nav);

  // Average over the short / long window, NAN when not gliding or not enough data yet.
  float shortRatio(uint32_t nowMs) const;
  float longRatio(uint32_t nowMs) const;

 private:
  struct Bin {
    uint32_t endMs;
    float speedMs;  // mean ground speed over the second
    float sinkMs;   // mean sink rate (positive = descending)
  };

  float ratioOver(uint32_t nowMs, int seconds, int minBins) const;

  Bin bins_[kMaxBins];
  int head_ = 0;   // next slot to write
  int count_ = 0;
  bool started_ = false;
  uint32_t binStartMs_ = 0;
  float sumSpeed_ = 0, sumSink_ = 0;
  int sumN_ = 0;
};
