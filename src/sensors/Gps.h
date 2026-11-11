#pragma once
#include <Arduino.h>

#include "Hardware.h"
#include "sensors/GpsData.h"

#if PV_HAS_GPS
#include <TinyGPSPlus.h>

#include <initializer_list>

// u-blox M10 on UART1. Finds the receiver's baud rate, then reconfigures it
// (115200 baud, airborne dynamic model, GGA+RMC only, configured nav rate).
//
// Everything else reads the receiver through read(), which copies the latest values into a
// GpsData; nothing outside this class uses the parser.
class Gps {
 public:
  void begin();
  // Feeds received characters to the parser and looks after the link (baud search, re-detect
  // after silence). Call every pass of the main loop.
  void update(uint32_t nowMs);
  // Latest values as plain data.
  void read(uint32_t nowMs, GpsData& out);

 private:
  struct KV {
    uint32_t key;
    uint32_t value;
  };

  void openBaud(uint32_t baud);
  void valset(std::initializer_list<KV> kvs);
  void sendConfig();
  void screenFix(uint32_t nowMs);
  bool steady(uint32_t nowMs) const;
  uint32_t utcEpoch();

  TinyGPSPlus gps_;
  HardwareSerial& ser_ = Serial1;
  uint32_t baud_ = 0;
  uint8_t baudIdx_ = 0;
  bool locked_ = false;
  uint32_t listenStartMs_ = 0;
  uint32_t checksumAtListen_ = 0;
  uint32_t lastCharMs_ = 0;
  bool havePrev_ = false;
  double prevLat_ = 0, prevLon_ = 0;
  float prevSpeed_ = 0;
  uint32_t prevFixMs_ = 0;
  uint32_t suspectMs_ = 0;  // last (re)acquisition or jump
};

#else  // no GPS fitted: nothing to drive, and read() reports "no link, no fix"

class Gps {
 public:
  void begin() {}
  void update(uint32_t) {}
  void read(uint32_t, GpsData& out) { out = GpsData(); }
};

#endif
