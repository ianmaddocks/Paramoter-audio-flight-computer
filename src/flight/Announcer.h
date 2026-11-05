#pragma once
#include "nav/Nav.h"

// Spoken height-boundary crossings, periodic height and periodic flight time.
// Climbing and descending have their own boundary lists (g_cfg.boundariesUp / g_cfg.boundaries).
class Announcer {
 public:
  void onTakeoff(const NavState& nav);
  void update(const NavState& nav, bool guidanceActive);

 private:
  static constexpr int kMaxBoundaries = 24;

  struct Boundaries {
    float list[kMaxBoundaries];
    int count = 0;
    char parsedFrom[128] = {1, 0};
    int band = 0;  // number of boundaries currently below the aircraft (with hysteresis)

    void parseIfChanged(const char* src);
    float at(int i) const;  // i-th boundary ascending; above the list repeats every boundaryStepM
    int bandFor(float h) const;
  };

  Boundaries up_, down_;
  uint32_t lastHeightMs_ = 0;
  uint32_t nextFlightTimeS_ = 0;
};
