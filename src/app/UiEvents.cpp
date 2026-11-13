#include "UiEvents.h"

#include <Arduino.h>
#include <math.h>

#include "App.h"
#include "Hardware.h"
#include "Log.h"
#include "Settings.h"

namespace uievents {
namespace {

constexpr uint32_t kWindowMs = 60000;
constexpr int kMaxPerWindow = 30;  // a tile flickering many times a minute is logged, then summarised

bool init = false;
bool lastMoving, lastLzArrow, lastWindsock, lastFlying, lastMuted, lastGps, lastBaroAlert, lastGpsAlert;
int lastBattAlert;  // 0 none, 1 low, 2 critical
const char* lastGpsClass = "";
const char* lastReady = "?";
uint32_t windowStartMs = 0;
int inWindow = 0, suppressed = 0;

// How the GPS tile reads the HDOP (WebPage.h)
const char* gpsClass(bool fix, float hdop) {
  if (!fix || !(hdop <= 20.0f)) return "none";
  return hdop <= 2 ? "strong" : hdop <= 5 ? "good" : hdop <= 10 ? "ok" : "poor";
}

bool allowed(uint32_t now) {
  if (now - windowStartMs >= kWindowMs) {
    if (suppressed) LOGI("UI", "%d more tile events in the last minute were not logged (flickering)", suppressed);
    windowStartMs = now;
    inWindow = suppressed = 0;
  }
  if (inWindow >= kMaxPerWindow) {
    ++suppressed;
    return false;
  }
  ++inWindow;
  return true;
}

}  // namespace

void reset() { init = false; }

void update(uint32_t now) {
  const NavState& n = app.nav;
  const bool gps = hw::hasGps();
  const bool moving = app.moving.on();  // the status tells the page this too (gps.moving)
  const bool lzArrow = moving && n.lzValid;
  const bool windsock = moving && app.wind.valid();
  const char* cls = gpsClass(n.fix, n.hdop);
  const char* ready = appNotReadyReason();
  const bool baroAlert = n.flying && !n.baroOk;
  const bool gpsAlert = n.flying && gps && !n.fix;
  const float bv = app.battery.volts();
  int battAlert = 0;
  if (bv >= 2.5f) {
    const float crit = fminf((float)g_cfg.battCriticalV, (float)g_cfg.battLowV);
    battAlert = bv < crit ? 2 : bv < g_cfg.battLowV ? 1 : 0;
  }
  if (!init) {
    init = true;
    lastMoving = moving;
    lastLzArrow = lzArrow;
    lastWindsock = windsock;
    lastFlying = n.flying;
    lastMuted = app.varioMuted;
    lastGps = gps;
    lastBaroAlert = baroAlert;
    lastGpsAlert = gpsAlert;
    lastBattAlert = battAlert;
    lastGpsClass = cls;
    lastReady = ready;
    windowStartMs = now;
    return;
  }
  if (moving != lastMoving) {
    lastMoving = moving;
    if (allowed(now)) {
      if (moving)
        LOGI("UI", "compass arrow shown (direction of travel): fix, ground speed %.2f m/s held at or above %.1f for %d s, track %.0f, %u satellites, hdop %.1f",
             n.speedMs, (double)g_cfg.moveShowMs, (int)g_cfg.moveHoldS, n.trackDeg, (unsigned)n.sats, n.hdop);
      else
        LOGI("UI", "compass arrow hidden: %s (ground speed %.2f m/s, %u satellites, hdop %.1f)",
             !gps ? "no GPS" : !n.fix ? "no fix" : "ground speed held under the hide limit", n.speedMs, (unsigned)n.sats, n.hdop);
    }
  }
  if (lzArrow != lastLzArrow) {
    lastLzArrow = lzArrow;
    if (allowed(now)) LOGI("UI", "landing zone arrow %s (landing zone %s, moving %d)", lzArrow ? "shown" : "hidden", n.lzValid ? "set" : "not set", moving);
  }
  if (windsock != lastWindsock) {
    lastWindsock = windsock;
    if (allowed(now))
      LOGI("UI", "windsock %s (wind %s %s %.0f/%.1f, moving %d)", windsock ? "shown" : "hidden", app.wind.valid() ? "known" : "unknown",
           app.wind.sourceName(), app.wind.fromDeg(), app.wind.speedMs(), moving);
  }
  if (n.flying != lastFlying) {
    lastFlying = n.flying;
    if (allowed(now)) LOGI("UI", "%s: Actions card for %s, height trace %s", n.flying ? "flying" : "grounded", n.flying ? "flight" : "the ground", n.flying ? "recording" : "stopped");
  }
  if (strcmp(cls, lastGpsClass) != 0) {
    if (allowed(now)) LOGI("UI", "GPS tile %s -> %s (fix %d, %u satellites, hdop %.1f)", lastGpsClass, cls, n.fix, (unsigned)n.sats, n.hdop);
    lastGpsClass = cls;
  }
  if (ready != lastReady) {
    if (allowed(now)) LOGI("UI", "ready banner: %s -> %s", lastReady ? lastReady : "ready", ready ? ready : "ready");
    lastReady = ready;
  }
  if (app.varioMuted != lastMuted) {
    lastMuted = app.varioMuted;
    if (allowed(now)) LOGI("UI", "vario %s", lastMuted ? "muted" : "unmuted");
  }
  if (baroAlert != lastBaroAlert) {
    lastBaroAlert = baroAlert;
    if (allowed(now)) LOGI("UI", "alert strip: barometer failed %s", baroAlert ? "shown" : "cleared");
  }
  if (gpsAlert != lastGpsAlert) {
    lastGpsAlert = gpsAlert;
    if (allowed(now)) LOGI("UI", "alert strip: no GPS fix %s (%u satellites, hdop %.1f)", gpsAlert ? "shown" : "cleared", (unsigned)n.sats, n.hdop);
  }
  if (battAlert != lastBattAlert) {
    lastBattAlert = battAlert;
    if (allowed(now)) LOGI("UI", "alert strip: battery %s (%.2f V)", battAlert == 2 ? "critical" : battAlert == 1 ? "low" : "alert cleared", bv);
  }
}

}  // namespace uievents
