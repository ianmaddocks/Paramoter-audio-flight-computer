#pragma once
#include "nav/Nav.h"

class Wind;

enum class ApPhase : uint8_t { Off, ToStart, Upwind, Crosswind, Downwind, Base, Final };

// Spoken landing-circuit guidance around the LZ.
//
// Circuit frame: origin at the LZ, +x pointing into wind (the final approach direction),
// +y to the right of +x. side = +1 for a right-hand circuit, -1 for left-hand.
//
//   start(xStart,0) --upwind--> (xCross,0) --crosswind--> (xCross, side*S)
//   --downwind--> (xBase, side*S) --base--> (xBase, 0) --final--> LZ (0,0)
//
// xBase is not fixed: during downwind it is recalculated from current height so that the
// engine-off glide along base and final ends at the LZ.
class Approach {
 public:
  bool active() const { return phase_ != ApPhase::Off; }
  ApPhase phase() const { return phase_; }
  const char* phaseName() const;

  bool start(const NavState& nav, const Wind& wind);
  void cancel(bool announce);
  void update(const NavState& nav, const Wind& wind);

 private:
  static const char* nameOf(ApPhase p);

  struct XY {
    float x, y;
  };

  XY toFrame(const NavState& nav) const;
  float bearingOf(float dx, float dy) const;
  float headingOf(float relDeg) const;
  float lead(const NavState& nav) const;
  void setPhase(ApPhase p, uint32_t now);
  void steerTo(const NavState& nav, float desiredDeg);
  void followLeg(const NavState& nav, XY pos, float px, float py, float relDeg);
  void turnCall(bool right);
  void callStartPoint(const NavState& nav, XY pos, bool first);

  ApPhase phase_ = ApPhase::Off;
  float upwindDeg_ = 0;
  int side_ = 0;
  float xStart_ = 0, xCross_ = 0, xBase_ = 0, xBaseNominal_ = 0;
  uint32_t phaseSinceMs_ = 0;
  uint32_t lastUpdateMs_ = 0;
  uint32_t offSinceMs_ = 0;
  // Steering calls wait while a turn is being made (F6): graceUntilMs_ covers the pilot's reaction after a
  // turn call; progressUntilMs_ is pushed on while the heading error is shrinking.
  uint32_t graceUntilMs_ = 0;
  uint32_t progressUntilMs_ = 0;
  uint32_t prevDiffMs_ = 0;
  float prevAbsDiff_ = 0;
  uint32_t lastSteerMs_ = 0;
  uint32_t lastGlideCallMs_ = 0;
  XY tracePos_{0, 0};
  float traceAgl_ = 0;
  uint32_t lastTraceMs_ = 0;
  bool descendCalled_ = false;
  bool tooLowCalled_ = false;
};
