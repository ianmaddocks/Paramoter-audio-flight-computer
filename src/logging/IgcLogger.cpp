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
  LOGI("IGC", "Logging to %s", path);
}

void IgcLogger::update(const NavState& nav, Gps& gps) {
  if (!file_ || !gps.timeValid()) return;
  if (nav.ms - lastRecordMs_ < 1000) return;
  lastRecordMs_ = nav.ms;
  if (nav.fix) {
    lastLat_ = nav.lat;
    lastLon_ = nav.lon;
  } else if (lastLat_ == 0 && lastLon_ == 0) {
    return;
  }
  double alat = fabs(lastLat_), alon = fabs(lastLon_);
  int latD = (int)alat, lonD = (int)alon;
  int latM = (int)lround((alat - latD) * 60000.0);
  int lonM = (int)lround((alon - lonD) * 60000.0);
  if (latM >= 60000) { ++latD; latM -= 60000; }
  if (lonM >= 60000) { ++lonD; lonM -= 60000; }
  file_.printf("B%02d%02d%02d%02d%05d%c%03d%05d%c%c%05d%05d\r\n", gps.time().hour(),
               gps.time().minute(), gps.time().second(), latD, latM, lastLat_ < 0 ? 'S' : 'N', lonD,
               lonM, lastLon_ < 0 ? 'W' : 'E', nav.fix ? 'A' : 'V', (int)lroundf(nav.altStdM),
               (int)lroundf(nav.gpsAltM));
  file_.printf("LXPVSAT,%02u\r\n", (unsigned)nav.sats);
  if (nav.ms - lastFlushMs_ >= 10000) {
    lastFlushMs_ = nav.ms;
    file_.flush();
  }
}

void IgcLogger::stop() {
  if (file_) file_.close();
}
