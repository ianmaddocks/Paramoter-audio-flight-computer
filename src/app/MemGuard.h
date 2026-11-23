#pragma once
#include <stdint.h>

// The last-resort answer to a memory leak: if free memory stays low for a long while, the vario saves its state and restarts
// itself, rather than running on until it crashes at a worse moment. The release build normally never goes below about 25 kB, so
// this should never act; it exists for the leak nobody has found yet.
//
//   * Only SUSTAINED low memory counts (holdS seconds below the limit); a brief dip while a page loads never does.
//   * In flight the limit is halved: a restart costs 10 to 15 s of silence, so it waits until a crash is nearer.
//   * Never in the first five minutes after a start, so a fault that comes back at once cannot make the vario restart in a loop.
struct MemGuard {
  static constexpr uint32_t kGraceMs = 5u * 60u * 1000u;

  // True when the vario should restart now. limitB = 0 turns the guard off.
  bool update(uint32_t nowMs, uint32_t freeB, bool flying, uint32_t limitB, uint32_t holdS) {
    if (limitB == 0 || nowMs < kGraceMs) {
      belowSinceMs_ = 0;
      return false;
    }
    if (freeB >= (flying ? limitB / 2 : limitB)) {
      belowSinceMs_ = 0;
      return false;
    }
    if (!belowSinceMs_) {
      belowSinceMs_ = nowMs ? nowMs : 1;
      return false;
    }
    return (uint32_t)(nowMs - belowSinceMs_) >= holdS * 1000u;
  }

 private:
  uint32_t belowSinceMs_ = 0;
};
