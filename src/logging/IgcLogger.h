#pragma once
#include <FS.h>

#include "nav/Nav.h"

class Gps;

// Writes a 1 Hz IGC track to /logs on LittleFS from takeoff to landing.
class IgcLogger {
 public:
  void start(Gps& gps);
  void update(const NavState& nav, Gps& gps);
  void stop();
  bool active() const { return (bool)file_; }

 private:
  void pruneOldLogs();

  File file_;
  uint32_t lastRecordMs_ = 0;
  uint32_t lastFlushMs_ = 0;
  double lastLat_ = 0, lastLon_ = 0;
};
