#include "Trace.h"

#include <Arduino.h>

#include "App.h"
#include "Hardware.h"
#include "Log.h"
#include "Settings.h"
#include "audio/Volume.h"
#include "sensors/Mic.h"

namespace trace {

#if PV_DEBUG
namespace {

constexpr uint32_t kNavIntervalMs = 1000;
constexpr uint32_t kBatIntervalMs = 2000;
constexpr uint32_t kVolIntervalMs = 5000;

uint32_t lastNavMs = 0, lastBatMs = 0, lastVolMs = 0;
const char* lastReadyWhy = "?";
int lastBleClients = 0;

void traceNav() {
  const NavState& n = app.nav;
  LOGD("NAV", "fix=%d good=%d sat=%u hdop=%.1f %.6f,%.6f gs=%.1f trk=%.0f gpsAlt=%.1f", n.fix, n.gpsGood, n.sats,
       n.hdop, n.lat, n.lon, n.speedMs, n.trackDeg, n.gpsAltM);
  LOGD("NAV", "baro=%d p=%.0f t=%.1f std=%.1f msl=%.1f agl=%.1f vs=%+.2f fly=%d ft=%lu", n.baroOk,
       n.pressurePa, n.tempC, n.altStdM, n.altMslM, n.aglM, n.varioMs, n.flying, (unsigned long)n.flightTimeS);
  LOGD("BARO", "rawStd=%.3f filtStd=%.3f raw-filt=%.3f dt=%.4f settled=%d", app.baro.rawAltStdM(),
       app.baro.altStdM(), app.baro.rawAltStdM() - app.baro.altStdM(), app.baro.sampleDtS(), app.baro.settled());
  LOGD("NAV", "lz=%d %.6f,%.6f elev=%.1f wind=%s %.0f/%.1f mode=%s/%s mute=%d batt=%.2fV heap=%u", n.lzValid,
       n.lzLat, n.lzLon, n.lzElevM, app.wind.sourceName(), app.wind.fromDeg(), app.wind.speedMs(),
       app.emergency.active() ? "emergency" : app.approach.active() ? "approach" : "none",
       app.approach.phaseName(), app.varioMuted, app.battery.volts(), ESP.getFreeHeap());
}

}  // namespace

void update(uint32_t now) {
  const char* why = appNotReadyReason();
  if (why != lastReadyWhy) {
    lastReadyWhy = why;
    LOGD("RDY", "%s", why ? why : "ready");
  }
  const int clients = app.ble.clients();
  if (clients != lastBleClients) {
    LOGD("BLE", "clients %d -> %d", lastBleClients, clients);
    lastBleClients = clients;
  }
  if (!dlog::tracing()) return;  // test build: quiet until something is reading the log
  if (now - lastBatMs >= kBatIntervalMs) {
    lastBatMs = now;
    LOGD("BAT", "v=%.3f pct=%d min=%d learned=%d", app.battery.volts(), app.battery.percent(),
         app.battery.minutesLeftOrUnknown(), app.battery.learned());
  }
  if (now - lastVolMs >= kVolIntervalMs) {
    lastVolMs = now;
    const int speech = volume::speechPct(), vario = volume::varioPct();
    LOGD("VOL", "noise %.1f dBFS, auto %s: speech %d%% (set %d%%, %+d), vario %d%% (set %d%%, %+d)",
         mic::noiseDb(), hw::hasMic() && g_cfg.autoVolume ? "on" : "off", speech, (int)g_cfg.speechVolume,
         speech - (int)g_cfg.speechVolume, vario, (int)g_cfg.varioVolume, vario - (int)g_cfg.varioVolume);
  }
  if (now - lastNavMs >= kNavIntervalMs) {
    lastNavMs = now;
    traceNav();
  }
}

// Logs whenever the lowest-ever free memory has dropped by 1.5 kB or more, naming the part of
// the loop that just ran, so a memory-hungry operation shows up in the log.
void memCheck(const char* after) {
  static uint32_t lastMin = UINT32_MAX;
  const uint32_t m = ESP.getMinFreeHeap();
  if (lastMin == UINT32_MAX) lastMin = m;
  if (m + 1500 <= lastMin) {
    LOGW("MEM", "lowest free memory %u -> %u B after %s (free now %u)", (unsigned)lastMin, (unsigned)m, after,
         (unsigned)ESP.getFreeHeap());
    lastMin = m;
  }
}

void reset() { lastReadyWhy = "?"; }

#else  // release build

void update(uint32_t) {}
void memCheck(const char*) {}
void reset() {}

#endif

}  // namespace trace
