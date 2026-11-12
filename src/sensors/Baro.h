#pragma once
#include <Adafruit_DPS310.h>
#include <Wire.h>

#include "sensors/BaroGuard.h"

// Two-state (altitude, vertical speed) Kalman filter driven by baro altitude.
struct AltKalman {
  float h = 0, v = 0;
  float p00 = 100, p01 = 0, p10 = 0, p11 = 10;
  bool init = false;

  void update(float z, float dt, float accelVar, float measVar);
};

class Baro {
 public:
  bool begin(TwoWire& wire);
  void update(uint32_t nowMs);

  bool ok() const { return ok_; }
  float pressurePa() const { return pressurePa_; }
  float tempC() const { return tempC_; }
  float rawAltStdM() const { return rawAltStdM_; }
  float sampleDtS() const { return sampleDtS_; }
  float altStdM() const { return kf_.h; }
  float varioMs() const { return kf_.v; }
  // False after power-on (or sensor reset) until the vario reading has settled; see update().
  bool settled() const { return settled_; }
  // Samples screened out (impossible, or lone outliers), persistent steps accepted, and whether the value
  // is frozen: for the status page and the tests.
  uint32_t rejected() const { return rejected_; }
  uint32_t jumps() const { return jumps_; }
  bool frozen() const { return guard_.stuck(); }

 private:
  bool configure();
  void updateSettled(uint32_t nowMs);
  void updateSim(uint32_t nowMs);
  bool screen(float pa, float altM, uint32_t nowMs, float dtS);

  Adafruit_DPS310 dps_;
  TwoWire* wire_ = nullptr;
  AltKalman kf_;
  bool ok_ = false;
  bool present_ = false;
  uint32_t lastSampleUs_ = 0;
  uint32_t lastGoodMs_ = 0;
  uint32_t lastRetryMs_ = 0;
  float pressurePa_ = 0;
  float tempC_ = 0;
  float rawAltStdM_ = 0;
  float sampleDtS_ = 0;
  uint16_t warmupLeft_ = 0;
  bool settled_ = false;
  uint32_t settleStartMs_ = 0;
  uint32_t quietSinceMs_ = 0;
  uint32_t simEpoch_ = 0;
  BaroGuard guard_;
  uint32_t rejected_ = 0, jumps_ = 0;
  uint32_t lastAcceptUs_ = 0;
  uint32_t lastRejectLogMs_ = 0;
  bool simLost_ = false;  // test build: the simulated sensor stopped delivering
};
