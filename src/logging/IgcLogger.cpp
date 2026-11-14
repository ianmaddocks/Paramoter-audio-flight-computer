#include "IgcLogger.h"
#include "Log.h"

#include <LittleFS.h>

#include <algorithm>
#include <vector>

#include "App.h"
#include "LogStore.h"
#include "Hardware.h"
#include "Settings.h"
#include "sensors/GpsData.h"
#include "audio/Volume.h"
#include "sensors/Mic.h"

namespace {
// Free space kept for a new flight when it starts (about 20 minutes of recording) and while it is recorded (checked every 30 s).
// Space is made as logstore::makeRoom says: logs of earlier days first, then today's thinned, then today's older logs, and
// last the start of this flight is trimmed: a flight never stops recording for want of room.
constexpr size_t kMinFreeBytes = 60 * 1024;
constexpr size_t kKeepFreeInFlight = 40 * 1024;
constexpr size_t kStopFreeBytes = 3 * 1024;       // under this even trimming found nothing to free (nothing but the stretch being written is left)
constexpr uint32_t kSegmentBytes = 32 * 1024;     // a stretch of a flight: about 10 minutes
const char* kDir = "/logs";
uint32_t gSegBytes = kSegmentBytes;
size_t gMinFree = kMinFreeBytes, gKeepFree = kKeepFreeInFlight;
}  // namespace

#if PV_TEST
namespace igctest {
void set(uint32_t segBytes, uint32_t minFree, uint32_t keepFree) {
  gSegBytes = segBytes ? segBytes : kSegmentBytes;
  gMinFree = minFree ? minFree : kMinFreeBytes;
  gKeepFree = keepFree ? keepFree : kKeepFreeInFlight;
}
}  // namespace igctest
#endif

void IgcLogger::start(const GpsData& gps) {
  if (!g_cfg.igcEnabled || file_) return;
  // An IGC record needs the time of day, which only the GPS knows. Without it (no GPS fitted, or
  // no reception yet) and with nothing buffered there is nothing to write: wait, and open the
  // file when the first record is ready, rather than leave an empty file behind.
  if (!gps.timeValid && preCount_ == 0) {
    pending_ = true;
    return;
  }
  open(gps);
}

void IgcLogger::open(const GpsData& gps) {
  pending_ = false;
  LittleFS.mkdir(kDir);
  if (gps.dateValid && gps.timeValid) {
    snprintf(id_, sizeof(id_), "%02d%02d%02d-%02d%02d%02d", gps.year % 100, gps.month, gps.day, gps.hour, gps.minute, gps.second);
  } else {
    snprintf(id_, sizeof(id_), "nodate-%08lu", (unsigned long)millis());
  }
  logstore::makeRoom(gMinFree, id_, "");
  // The header goes in a file of its own (<id>.hdr), the fixes in stretches after it (<id>.s000, .s001 ...): see LogStore.h
  {
    File h = LittleFS.open(String(kDir) + "/" + id_ + ".hdr", "w");
    if (!h) {
      LOGE("IGC", "Cannot create the log %s", id_);
      return;
    }
    h.print("AXPVParaVario\r\n");
    if (gps.dateValid) h.printf("HFDTEDATE:%02d%02d%02d,01\r\n", gps.day, gps.month, gps.year % 100);
    h.printf("HFPLTPILOTINCHARGE:%s\r\n", g_cfg.pilotName);
    h.printf("HFGTYGLIDERTYPE:%s\r\n", g_cfg.gliderType);
    h.print("HFDTMGPSDATUM:WGS84\r\n");
    h.print("HFFTYFRTYPE:ParaVario,XIAO ESP32C3\r\n");
    h.print("HFGPSRECEIVER:u-blox,M10\r\n");
    h.print("HFPRSPRESSALTSENSOR:Infineon,DPS310\r\n");
    h.print("I033639RPM4042VVL4345SVL\r\n");
    h.printf("LXPVVOL,%d,%d,%d,%d\r\n", (int)lroundf(g_cfg.noiseQuietDb), (int)lroundf(g_cfg.noiseLoudDb), g_cfg.autoVolume ? 1 : 0,
             (int)g_cfg.volumeRampPctS);
    const NavState& n = app.nav;
    h.printf("LXPVGND,%d\r\n", (int)lroundf(n.altMslM - n.aglM));
    if (n.lzValid) h.printf("LXPVLZ,%.6f,%.6f,%d\r\n", n.lzLat, n.lzLon, (int)lroundf(n.lzElevM));
    else h.print("LXPVLZ,none\r\n");
    h.close();
  }
  seg_ = 0;
  openSegment();
  if (!file_) return;
  // lastRecordMs_ is kept: the 1 Hz rhythm carries on from the buffered fixes, so the first live
  // record cannot share a second with the last buffered one.
  lastFlushMs_ = 0;
  // The history leading up to takeoff, oldest first.
  int n = preCount_;
  for (int i = 0; i < n; ++i) writeSample(pre_[(preHead_ + i) % kMaxPreSamples]);
  preCount_ = preHead_ = 0;
  file_.print("LXPVSTART\r\n");  // the pre-takeoff history ends here: this is takeoff
  lastSlowMs_ = 0;
  lastSpaceCheckMs_ = millis();
  LOGI("IGC", "Logging to %s (%d s of pre-takeoff history)", id_, n);
}

// Opens the next stretch of the flight: <id>.s000, .s001 ...
void IgcLogger::openSegment() {
  snprintf(activePiece_, sizeof(activePiece_), "%s.s%03d", id_, seg_);
  file_ = LittleFS.open(String(kDir) + "/" + activePiece_, "w");
  if (!file_) LOGE("IGC", "Cannot create %s", activePiece_);
}

void IgcLogger::rollSegment() {
  file_.close();
  ++seg_;
  openSegment();
}

void IgcLogger::writeSample(const Sample& s) {
  int32_t alat = abs(s.latMin), alon = abs(s.lonMin);
  file_.printf("B%02u%02u%02u%02d%05d%c%03d%05d%c%c%05d%05d%04u%03u%03u\r\n", s.hour, s.min, s.sec, (int)(alat / 60000),
               (int)(alat % 60000), s.latMin < 0 ? 'S' : 'N', (int)(alon / 60000), (int)(alon % 60000),
               s.lonMin < 0 ? 'W' : 'E', s.valid ? 'A' : 'V', (int)s.altStd, (int)s.altGps, (unsigned)s.rpm, (unsigned)s.vVol, (unsigned)s.sVol);
}

// The slow values, every 5 s: flags (1 flying, 2 muted, 4 GPS good, 8 barometer ok, 16 microphone, 32 automatic volume), wind from
// (degrees) and speed (0.1 m/s) or -1, -1; battery (0.01 V, percent, minutes left or -1); satellites; HDOP x 10; background noise
// (dB below full scale); the speech and vario volume settings (percent), which the applied volumes in the B records rise from.
void IgcLogger::writeSlow(const NavState& nav) {
  const bool wind = app.wind.valid();
  const int flags = (nav.flying ? 1 : 0) | (app.varioMuted ? 2 : 0) | (nav.gpsGood ? 4 : 0) | (nav.baroOk ? 8 : 0) |
                    (hw::hasMic() ? 16 : 0) | (g_cfg.autoVolume ? 32 : 0);
  const float noise = hw::hasMic() ? mic::noiseDb() : NAN;
  file_.printf("LXPVD,%d,%d,%d,%d,%d,%d,%u,%d,%d,%d,%d\r\n", flags, wind ? (int)lroundf(app.wind.fromDeg()) : -1,
               wind ? (int)lroundf(app.wind.speedMs() * 10.0f) : -1, (int)lroundf(app.battery.volts() * 100.0f),
               (int)app.battery.percent(), (int)app.battery.minutesLeftOrUnknown(), (unsigned)nav.sats,
               (int)lroundf(constrain(nav.hdop, 0.0f, 99.0f) * 10.0f), isfinite(noise) ? (int)lroundf(-noise) : 0,
               (int)g_cfg.speechVolume, (int)g_cfg.varioVolume);
}

// Keeps only the newest igcPreS samples; the buffer lives in RAM and is never written to flash.
void IgcLogger::pushPre(const Sample& s) {
  int cap = constrain((int)g_cfg.igcPreS, 0, kMaxPreSamples);
  if (cap == 0) {
    preCount_ = preHead_ = 0;
    return;
  }
  while (preCount_ >= cap) {
    preHead_ = (preHead_ + 1) % kMaxPreSamples;
    --preCount_;
  }
  pre_[(preHead_ + preCount_) % kMaxPreSamples] = s;
  ++preCount_;
}

void IgcLogger::update(const NavState& nav, const GpsData& gps) {
  if ((!file_ && !g_cfg.igcEnabled) || !gps.timeValid) return;
  if (nav.ms - lastRecordMs_ < 1000) return;
  lastRecordMs_ = nav.ms;
  if (nav.fix) {
    lastLat_ = nav.lat;
    lastLon_ = nav.lon;
  } else if (lastLat_ == 0 && lastLon_ == 0) {
    return;
  }
  auto metres = [](float m) { return isfinite(m) ? (int32_t)constrain(lroundf(m), -9999L, 99999L) : 0; };
  Sample s;
  s.hour = gps.hour;
  s.min = gps.minute;
  s.sec = gps.second;
  s.sats = nav.sats;
  {
    const float r = hw::hasMic() ? mic::rpm() : 0.0f;
    s.rpm = (uint16_t)constrain(isfinite(r) ? lroundf(r) : 0L, 0L, 9999L);
  }
  s.vVol = min<uint8_t>(100, volume::varioPct());
  s.sVol = min<uint8_t>(100, volume::speechPct());
  s.valid = nav.fix;
  s.latMin = (int32_t)lround(lastLat_ * 60000.0);
  s.lonMin = (int32_t)lround(lastLon_ * 60000.0);
  // Viewers reject the negative values a true 1013.25 hPa altitude gives on a high-pressure day,
  // so by default log the baro altitude referenced to GPS (QNH-style), never below zero.
  s.altStd = g_cfg.igcQnhAlt ? max<int32_t>(0, metres(nav.altMslM)) : metres(nav.altStdM);
  s.altGps = metres(nav.gpsAltM);
  if (!file_ && pending_) open(gps);  // a flight that began before the GPS had the time
  if (!file_) {
    pushPre(s);
    return;
  }
  writeSample(s);
  if (!lastSlowMs_ || nav.ms - lastSlowMs_ >= 5000) {
    lastSlowMs_ = nav.ms;
    writeSlow(nav);
  }
  if (file_.size() >= gSegBytes) {  // a stretch is full: the next one
    rollSegment();
    if (!file_) return;
  }
  if (nav.ms - lastSpaceCheckMs_ >= 30000) {  // a long flight: room is made before this one runs out of it, by thinning and deleting
    lastSpaceCheckMs_ = nav.ms;  // older logs, and in the end by trimming the start of this flight; it does not stop recording
    if (logstore::freeBytes() < gKeepFree) logstore::makeRoom(gKeepFree, id_, activePiece_);
    if (logstore::freeBytes() < kStopFreeBytes) {
      LOGE("IGC", "No room at all, recording stopped");
      file_.print("LXPVEND\r\n");
      file_.close();
      return;
    }
  }
  if (nav.ms - lastFlushMs_ >= 10000) {
    lastFlushMs_ = nav.ms;
    file_.flush();
  }
}

void IgcLogger::stop() {
  pending_ = false;
  if (file_) {
    file_.print("LXPVEND\r\n");
    file_.close();
  }
}
