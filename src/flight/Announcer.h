#pragma once
#include "nav/Nav.h"

// Spoken height-boundary crossings, periodic height and periodic flight time.
class Announcer {
 public:
  void onTakeoff(const NavState& nav);
  void update(const NavState& nav, bool guidanceActive);

 private:
  static constexpr int kMaxBoundaries = 24;

  void parseIfChanged();
  float boundary(int i) const;
  int bandFor(float h) const;

  float list_[kMaxBoundaries];
  int count_ = 0;
  char parsedFrom_[128] = {1, 0};
  int band_ = 0;
  uint32_t lastHeightMs_ = 0;
  uint32_t nextFlightTimeS_ = 0;
};
