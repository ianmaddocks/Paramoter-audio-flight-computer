#include "IgcLogger.h"
#include "Log.h"

#include <LittleFS.h>

#include <algorithm>
#include <vector>

#include "Settings.h"
#include "sensors/Gps.h"

namespace {
constexpr size_t kMinFreeBytes = 300 * 1024;
const char* kDir = "/logs";
}  // namespace

void IgcLogger::pruneOldLogs() {
  std::vector<String> names;
  File dir = LittleFS.open(kDir);
  if (!dir) return;
  for (File f = dir.openNextFile(); f; f = dir.openNextFile()) names.push_back(f.name());
  dir.close();
  std::sort(names.begin(), names.end());
  for (const String& n : names) {
    if (LittleFS.totalBytes() - LittleFS.usedBytes() >= kMinFreeBytes) break;
    LittleFS.remove(String(kDir) + "/" + n);
    LOGI("IGC", "Deleted old log %s", n.c_str());
  }
}

void IgcLogger::start(Gps& gps) {
  if (!g_cfg.igcEnabled || file_) return;
  LittleFS.mkdir(kDir);
  pruneOldLogs();

  char path[40];
  if (gps.dateValid() && gps.timeValid()) {
    snprintf(path, sizeof(path), "%s/%02d%02d%02d-%02d%02d%02d.igc", kDir, gps.date().year() % 100,
             gps.date().month(), gps.date().day(), gps.time().hour(), gps.time().minute(),
             gps.time().second());
  } else {
    snprintf(path, sizeof(path), "%s/nodate-%08lu.igc", kDir, (unsigned long)millis());
  }
  file_ = LittleFS.open(path, "w");
  if (!file_) {
    LOGE("IGC", "Cannot create %s", path);
    return;
  }
  file_.print("AXPVParaVario\r\n");
  if (gps.dateValid()) {
    file_.printf("HFDTEDATE:%02d%02d%02d,01\r\n", gps.date().day(), gps.date().month(),
                 gps.date().year() % 100);
  }
  file_.printf("HFPLTPILOTINCHARGE:%s\r\n", g_cfg.pilotName);
  file_.printf("HFGTYGLIDERTYPE:%s\r\n", g_cfg.gliderType);
  file_.print("HFDTMGPSDATUM:WGS84\r\n");
  file_.print("HFFTYFRTYPE:ParaVario,XIAO ESP32C3\r\n");
  file_.print("HFGPSRECEIVER:u-blox,M10\r\n");
  file_.print("HFPRSPRESSALTSENSOR:Infineon,DPS310\r\n");
  lastRecordMs_ = lastFlushMs_ = 0;
  // The history leading up to takeoff, oldest first.
  int n = preCount_;
  for (int i = 0; i < n; ++i) writeSample(pre_[(preHead_ + i) % kMaxPreSamples]);
  preCount_ = preHead_ = 0;
  LOGI("IGC", "Logging to %s (%d s of pre-takeoff history)", path, n);
}

void IgcLogger::writeSample(const Sample& s) {
  int32_t alat = abs(s.latMin), alon = abs(s.lonMin);
  file_.printf("B%02u%02u%02u%02d%05d%c%03d%05d%c%c%05d%05d\r\n", s.hour, s.min, s.sec, (int)(alat / 60000),
               (int)(alat % 60000), s.latMin < 0 ? 'S' : 'N', (int)(alon / 60000), (int)(alon % 60000),
               s.lonMin < 0 ? 'W' : 'E', s.valid ? 'A' : 'V', (int)s.altStd, (int)s.altGps);
  file_.printf("LXPVSAT,%02u\r\n", (unsigned)s.sats);
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

void IgcLogger::update(const NavState& nav, Gps& gps) {
  if ((!file_ && !g_cfg.igcEnabled) || !gps.timeValid()) return;
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
  s.hour = gps.time().hour();
  s.min = gps.time().minute();
  s.sec = gps.time().second();
  s.sats = nav.sats;
  s.valid = nav.fix;
  s.latMin = (int32_t)lround(lastLat_ * 60000.0);
  s.lonMin = (int32_t)lround(lastLon_ * 60000.0);
  // Viewers reject the negative values a true 1013.25 hPa altitude gives on a high-pressure day,
  // so by default log the baro altitude referenced to GPS (QNH-style), never below zero.
  s.altStd = g_cfg.igcQnhAlt ? max<int32_t>(0, metres(nav.altMslM)) : metres(nav.altStdM);
  s.altGps = metres(nav.gpsAltM);
  if (!file_) {
    pushPre(s);
    return;
  }
  writeSample(s);
  if (nav.ms - lastFlushMs_ >= 10000) {
    lastFlushMs_ = nav.ms;
    file_.flush();
  }
}

void IgcLogger::stop() {
  if (file_) file_.close();
}
