#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>

#include <initializer_list>

#include "Hardware.h"
#include "sensors/GpsData.h"

// u-blox M10 on UART1. Finds the receiver's baud rate, then reconfigures it
// (115200 baud, airborne dynamic model, GGA+RMC only, configured nav rate).
//
// The same code also finds out whether a receiver is there at all (see Hardware.h): update()
// keeps hw::gpsState() current. Everything else reads the receiver through read(), which copies
// the latest values into a GpsData; nothing outside this class uses the parser. With no receiver
// the data simply stays "no link, no fix".
class Gps {
 public:
  void begin();
  // Feeds received characters to the parser and looks after the link (baud search, re-detect
  // after silence) and the detection of the module. Call every pass of the main loop.
  void update(uint32_t nowMs);
  // Latest values as plain data.
  void read(uint32_t nowMs, GpsData& out);

#if PV_TEST
  // Test build: forget that a GPS was ever seen and start the detection time again.
  void forgetForTest();
#endif

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
  void pollLink(uint32_t nowMs);
  void updatePresence(uint32_t nowMs);
  void start();
  void stop();

  TinyGPSPlus gps_;
  HardwareSerial& ser_ = Serial1;
  bool active_ = false;       // the serial port is open
  uint32_t startMs_ = 0;      // when detection began (power-on, or a restart of it)
  bool seen_ = false;         // valid sentences have arrived since power-on
  bool seenEver_ = false;     // a GPS has been seen on this unit (kept in flash)
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
