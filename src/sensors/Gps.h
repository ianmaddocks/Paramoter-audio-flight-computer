#pragma once
#include <Arduino.h>
#include <TinyGPSPlus.h>

#include <initializer_list>

// u-blox M10 on UART1. Finds the receiver's baud rate, then reconfigures it
// (115200 baud, airborne dynamic model, GGA+RMC only, configured nav rate).
class Gps {
 public:
  void begin();
  void update(uint32_t nowMs);

  bool fix() const;
  double lat() { return gps_.location.lat(); }
  double lon() { return gps_.location.lng(); }
  bool altValid() { return gps_.altitude.isValid() && gps_.altitude.age() < 3000; }
  float altM() { return gps_.altitude.meters(); }
  float speedMs() { return gps_.speed.mps(); }
  float courseDeg() { return gps_.course.deg(); }
  uint8_t sats() { return gps_.satellites.value(); }
  float hdop() { return gps_.hdop.isValid() ? gps_.hdop.hdop() : 99.0f; }
  bool connected() const { return locked_; }
  uint32_t baud() const { return baud_; }

  bool timeValid() { return gps_.time.isValid() && gps_.time.age() < 3000; }
  bool dateValid() { return gps_.date.isValid() && gps_.date.year() > 2020; }
  TinyGPSDate& date() { return gps_.date; }
  TinyGPSTime& time() { return gps_.time; }
  uint32_t utcEpoch();  // seconds since 1970, 0 if date/time unknown

 private:
  struct KV {
    uint32_t key;
    uint32_t value;
  };

  void openBaud(uint32_t baud);
  void valset(std::initializer_list<KV> kvs);
  void sendConfig();

  TinyGPSPlus gps_;
  HardwareSerial& ser_ = Serial1;
  uint32_t baud_ = 0;
  uint8_t baudIdx_ = 0;
  bool locked_ = false;
  uint32_t listenStartMs_ = 0;
  uint32_t checksumAtListen_ = 0;
  uint32_t lastCharMs_ = 0;
};
