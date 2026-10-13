#pragma once
#include <stddef.h>

#include "nav/Nav.h"

class Gps;
class NimBLECharacteristic;
class NimBLEServer;

// BLE telemetry on the Nordic UART service, in the format proven with FlyGaggle:
// pseudo-GPS $GPGGA/$GPRMC (barometric altitude), $LK8EX1 and $LXWP0.
class BleFeed {
 public:
  void begin();
  void update(const NavState& nav, Gps& gps, int batteryPct, bool recording);
  int clients() const;

 private:
  void sendGps(const NavState& nav, Gps& gps);
  void sendVario(const NavState& nav, int batteryPct, bool recording);
  void sendBytes(const char* data, size_t len);

  NimBLEServer* server_ = nullptr;
  NimBLECharacteristic* tx_ = nullptr;
  uint32_t lastGpsMs_ = 0;
  uint32_t lastVarioMs_ = 0;
  uint32_t lastAvgMs_ = 0;
  float vario30s_ = 0;
};
