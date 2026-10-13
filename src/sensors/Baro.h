#pragma once
#include <Adafruit_DPS310.h>
#include <Wire.h>

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
  float altStdM() const { return kf_.h; }
  float varioMs() const { return kf_.v; }

 private:
  bool configure();

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
};
