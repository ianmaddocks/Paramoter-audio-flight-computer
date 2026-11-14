#include "IgcLogger.h"
#include "Log.h"

#include <LittleFS.h>

#include <algorithm>
#include <vector>

#include "App.h"
#include "Hardware.h"
#include "Settings.h"
#include "sensors/GpsData.h"
#include "audio/Volume.h"
#include "sensors/Mic.h"

namespace {
// Free space kept for a new flight when it starts: about 20 minutes of recording. The oldest logs are deleted, oldest first,
// only as far as needed to get there (never more). (This was 300 kB, which is more than the whole free space left beside the
// voice clips: every new flight deleted every earlier one.)
constexpr size_t kMinFreeBytes = 60 * 1024;
constexpr size_t kKeepFreeInFlight = 40 * 1024;  // checked every 30 s while recording: older logs go before the flight stops
constexpr size_t kStopFreeBytes = 6 * 1024;      // under this the file would not close cleanly: recording stops
const char* kDir = "/logs";
}  // namespace

// Deletes the oldest logs (not `keep`) until minFreeBytes are free.
void IgcLogger::pruneOldLogs(size_t minFreeBytes, const char* keep) {
  std::vector<String> names;
  File dir = LittleFS.open(kDir);
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) names.push_back(f.name());
  dir.close();
  std::sort(names.begin(), names.end());
  for (const String& n : names) {
    if (LittleFS.totalBytes() - LittleFS.usedBytes() >= minFreeBytes) break;
    if (keep && n == keep) continue;
    LittleFS.remove(String(kDir) + "/" + n);
    LOGI("IGC", "Deleted old log %s to make room (%u kB free)", n.c_str(),
         (unsigned)((LittleFS.totalBytes() - LittleFS.usedBytes()) / 1024));
  }
}

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
  pruneOldLogs(kMinFreeBytes, nullptr);

  char path[40];
  if (gps.dateValid && gps.timeValid) {
    snprintf(path, sizeof(path), "%s/%02d%02d%02d-%02d%02d%02d.igc", kDir, gps.year % 100, gps.month, gps.day,
             gps.hour, gps.minute, gps.second);
  } else {
    snprintf(path, sizeof(path), "%s/nodate-%08lu.igc", kDir, (unsigned long)millis());
  }
  strlcpy(path_, path, sizeof(path_));
  file_ = LittleFS.open(path, "w");
  if (!file_) {
    LOGE("IGC", "Cannot create %s", path);
    return;
  }
  file_.print("AXPVParaVario\r\n");
  if (gps.dateValid) file_.printf("HFDTEDATE:%02d%02d%02d,01\r\n", gps.day, gps.month, gps.year % 100);
  file_.printf("HFPLTPILOTINCHARGE:%s\r\n", g_cfg.pilotName);
  file_.printf("HFGTYGLIDERTYPE:%s\r\n", g_cfg.gliderType);
  file_.print("HFDTMGPSDATUM:WGS84\r\n");
  file_.print("HFFTYFRTYPE:ParaVario,XIAO ESP32C3\r\n");
  file_.print("HFGPSRECEIVER:u-blox,M10\r\n");
  file_.print("HFPRSPRESSALTSENSOR:Infineon,DPS310\r\n");
  file_.print("I033639RPM4042VVL4345SVL\r\n");  // each B record ends with the engine RPM (4 digits), the vario and speech volume applied (3 digits each, %)
  file_.printf("LXPVVOL,%d,%d,%d,%d\r\n", (int)lroundf(g_cfg.noiseQuietDb), (int)lroundf(g_cfg.noiseLoudDb), g_cfg.autoVolume ? 1 : 0,
               (int)g_cfg.volumeRampPctS);  // how the loudness adjustment was set: quiet / loud noise limits (dBFS), automatic, ramp (%/s)
  {  // what the Flight screen needs that is not in the fixes: the ground height and the landing zone
    const NavState& n = app.nav;
    file_.printf("LXPVGND,%d\r\n", (int)lroundf(n.altMslM - n.aglM));
    if (n.lzValid) file_.printf("LXPVLZ,%.6f,%.6f,%d\r\n", n.lzLat, n.lzLon, (int)lroundf(n.lzElevM));
    else file_.print("LXPVLZ,none\r\n");
  }
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
  LOGI("IGC", "Logging to %s (%d s of pre-takeoff history)", path, n);
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
  if (nav.ms - lastSpaceCheckMs_ >= 30000) {  // a long flight: older logs make way before this one runs out of room
    lastSpaceCheckMs_ = nav.ms;
    const char* slash = strrchr(path_, '/');
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < kKeepFreeInFlight) pruneOldLogs(kKeepFreeInFlight, slash ? slash + 1 : path_);
    if (LittleFS.totalBytes() - LittleFS.usedBytes() < kStopFreeBytes) {
      LOGW("IGC", "No room left: recording stopped");
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
