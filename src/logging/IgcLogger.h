#pragma once
#include <FS.h>

#include "nav/Nav.h"

class Gps;

// Writes a 1 Hz IGC track to /logs on LittleFS from takeoff to landing. Until then it keeps the
// last few seconds of fixes in a RAM ring buffer (never flash) and writes them at the start of
// the log, so the file does not miss the takeoff run.
class IgcLogger {
 public:
  void start(Gps& gps);
  void update(const NavState& nav, Gps& gps);
  void stop();
  bool active() const { return (bool)file_; }

  // Longest pre-takeoff history that can be kept (the setting is clamped to this).
  static constexpr int kMaxPreSamples = 120;

 private:
  // One 1 Hz fix, stored compactly (position in 0.001 minute units, as written to the file).
  struct Sample {
    uint8_t hour, min, sec;
    uint8_t sats;
    bool valid;
    int32_t latMin, lonMin;
    int32_t altStd, altGps;
  };

  void pruneOldLogs();
  void writeSample(const Sample& s);
  void pushPre(const Sample& s);

  File file_;
  uint32_t lastRecordMs_ = 0;
  uint32_t lastFlushMs_ = 0;
  double lastLat_ = 0, lastLon_ = 0;
  Sample pre_[kMaxPreSamples];
  int preHead_ = 0;   // index of the oldest sample
  int preCount_ = 0;
};
