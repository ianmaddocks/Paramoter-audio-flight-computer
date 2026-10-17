#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>

#include "App.h"
#include "Log.h"
#include "Pins.h"
#include "Settings.h"
#include "audio/Audio.h"
#include "audio/Speech.h"
#include "comms/WebUi.h"

using audio::Priority;

App app;

namespace {

constexpr uint32_t kTickMs = 100;
constexpr uint32_t kToneTickMs = 50;
constexpr uint32_t kGpsLostCallMs = 5000;
constexpr uint32_t kBattLowRepeatMs = 5 * 60 * 1000;
constexpr uint32_t kSaveIntervalMs = 60000;
constexpr uint32_t kStaleRecordS = 15 * 60;
constexpr uint32_t kTraceIntervalMs = 1000;

uint32_t lastTickMs = 0;
uint32_t lastToneMs = 0;
bool hadFix = false;
uint32_t fixLostMs = 0;
bool fixLostCalled = false;
uint32_t battLowSinceMs = 0;
uint32_t lastBattCallMs = 0;
uint32_t lastSaveMs = 0;
uint32_t lastTraceMs = 0;
int lastBleClients = 0;

void traceNav() {
#if PV_DEBUG
  const NavState& n = app.nav;
  LOGD("NAV", "fix=%d good=%d sat=%u hdop=%.1f %.6f,%.6f gs=%.1f trk=%.0f gpsAlt=%.1f", n.fix, n.gpsGood, n.sats,
       n.hdop, n.lat, n.lon, n.speedMs, n.trackDeg, n.gpsAltM);
  LOGD("NAV", "baro=%d p=%.0f t=%.1f std=%.1f msl=%.1f agl=%.1f vs=%+.2f fly=%d ft=%lu", n.baroOk,
       n.pressurePa, n.tempC, n.altStdM, n.altMslM, n.aglM, n.varioMs, n.flying,
       (unsigned long)n.flightTimeS);
  LOGD("NAV", "lz=%d %.6f,%.6f elev=%.1f wind=%s %.0f/%.1f mode=%s/%s mute=%d batt=%.2fV heap=%u",
       n.lzValid, n.lzLat, n.lzLon, n.lzElevM, app.wind.sourceName(), app.wind.fromDeg(),
       app.wind.speedMs(), app.emergency.active() ? "emergency" : app.approach.active() ? "approach" : "none",
       app.approach.phaseName(), app.varioMuted, app.battery.volts(), ESP.getFreeHeap());
  int clients = app.ble.clients();
  if (clients != lastBleClients) {
    LOGD("BLE", "clients %d -> %d", lastBleClients, clients);
    lastBleClients = clients;
  }
#endif
}
bool resumeUnverified = false;
uint32_t resumeSavedUtc = 0;

void saveFlightState() {
  FlightRecord rec = {};
  app.flight.exportState(rec);
  app.wind.exportState(rec);
  rec.savedUtc = app.gps.utcEpoch();
  flightstore::save(rec);
  lastSaveMs = millis();
}

void restoreFlightState() {
  FlightRecord rec;
  if (!flightstore::load(rec)) return;
  app.flight.importState(rec, millis());
  if (!rec.flying) return;
  app.wind.importState(rec);
  resumeUnverified = true;
  resumeSavedUtc = rec.savedUtc;
  LOGI("APP", "Restored in-flight state: %lu s flown, LZ %.6f,%.6f", (unsigned long)rec.flightTimeS,
        rec.lzLat, rec.lzLon);
}

// A restored flight is checked against GPS time once available: a stale record (device
// switched off in the air state and powered up much later) is dropped; otherwise the time
// spent rebooting is added to the flight time.
void verifyResume() {
  if (!resumeUnverified || !app.nav.flying) {
    resumeUnverified = false;
    return;
  }
  uint32_t nowUtc = app.gps.utcEpoch();
  if (!nowUtc) return;
  resumeUnverified = false;
  if (!resumeSavedUtc) return;
  uint32_t gap = nowUtc >= resumeSavedUtc ? nowUtc - resumeSavedUtc : UINT32_MAX;
  if (gap > kStaleRecordS) {
    LOGI("APP", "Saved flight is stale (%lu s old), discarding", (unsigned long)gap);
    app.flight.abandonFlight();
    app.approach.cancel(false);
    app.emergency.cancel(false);
    app.logger.stop();
    saveFlightState();
  } else {
    app.flight.addFlightTime(gap);
  }
}

void updateVarioTone() {
  const NavState& n = app.nav;
  bool allowed = g_cfg.varioEnabled && !app.varioMuted && n.baroOk &&
                 (!g_cfg.varioOnlyInFlight || n.flying);
  float v = n.varioMs;
  if (allowed && g_cfg.varioClimbBeeps && v >= g_cfg.climbThreshold) {
    float hz = constrain(g_cfg.varioBaseHz + g_cfg.varioHzPerMs * v, 200.0f, 2500.0f);
    uint16_t period = (uint16_t)constrain(500.0f - 70.0f * v, 150.0f, 500.0f);
    audio::setTone(hz, period / 2, period / 2);
  } else if (allowed && g_cfg.varioSinkAlarm && v <= g_cfg.sinkThreshold) {
    float hz = constrain(g_cfg.varioBaseHz * 0.5f + 20.0f * v, 150.0f, 1000.0f);
    audio::setTone(hz, 0, 0);
  } else {
    audio::setTone(0, 0, 0);
  }
}

void handleFlightEvent(FlightEvent ev) {
  if (ev == FlightEvent::Takeoff) {
    app.wind.onTakeoff(app.flight.takeoffCourseValid(), app.flight.takeoffCourseDeg());
    app.announcer.onTakeoff(app.nav);
    app.logger.start(app.gps);
    speech::say(Priority::High, "takeoff");
    saveFlightState();
    LOGI("APP", "Takeoff, course %.0f", app.flight.takeoffCourseDeg());
  } else if (ev == FlightEvent::Resumed) {
    app.announcer.onTakeoff(app.nav);
    app.logger.start(app.gps);
    speech::say(Priority::High, "flight_resumed");
  } else if (ev == FlightEvent::Landing) {
    saveFlightState();
    app.approach.cancel(false);
    app.emergency.cancel(false);
    app.logger.stop();
    audio::Utterance u(Priority::High);
    u.add("landed").add("flight_time");
    speech::addDuration(u, app.nav.flightTimeS);
    audio::say(u);
    LOGI("APP", "Landed after %lu s", (unsigned long)app.nav.flightTimeS);
  }
}

void checkGps(uint32_t now) {
  if (app.nav.fix) {
    if (!hadFix) speech::say(Priority::Normal, "gps_fix");
    hadFix = true;
    fixLostMs = 0;
    fixLostCalled = false;
    return;
  }
  if (!hadFix || !app.nav.flying) return;
  if (!fixLostMs) fixLostMs = now;
  if (!fixLostCalled && now - fixLostMs >= kGpsLostCallMs) {
    fixLostCalled = true;
    speech::say(Priority::High, "no_gps");
  }
}

void checkBattery(uint32_t now) {
  float v = app.battery.volts();
  if (v < 2.5f || v >= g_cfg.battLowV) {  // < 2.5 V: running on USB / not connected
    battLowSinceMs = 0;
    return;
  }
  if (!battLowSinceMs) battLowSinceMs = now;
  if (now - battLowSinceMs >= 10000 && (!lastBattCallMs || now - lastBattCallMs >= kBattLowRepeatMs)) {
    lastBattCallMs = now;
    speech::say(Priority::Normal, "battery_low");
  }
}

}  // namespace

void actionToggleApproach() {
  if (app.emergency.active()) return;
  if (app.approach.active()) app.approach.cancel(true);
  else app.approach.start(app.nav, app.wind);
}

void actionToggleEmergency() {
  if (app.emergency.active()) {
    app.emergency.cancel(true);
  } else {
    app.approach.cancel(false);
    app.emergency.start(app.nav, app.wind);
  }
}

void actionToggleMute() {
  app.varioMuted = !app.varioMuted;
  speech::say(Priority::Urgent, app.varioMuted ? "vario_muted" : "vario_on");
}

void setup() {
  Serial.setTxBufferSize(2048);  // before begin(), which otherwise allocates 256 bytes
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block when no USB host is listening
  LOGI("APP", "ParaVario starting (%s build), reset reason %d", dlog::kDebugBuild ? "debug" : "release",
       (int)esp_reset_reason());
  settings::load();
  if (!LittleFS.begin(true)) LOGE("APP", "LittleFS mount failed");

  audio::begin();
  audio::setVolumes(g_cfg.speechVolume, g_cfg.varioVolume);

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  app.baro.begin(Wire);
  app.gps.begin();
  app.battery.begin();
  app.button.begin(pins::BUTTON);
  app.ble.begin();
  webui::begin();
  restoreFlightState();

  speech::say(Priority::Normal, "system_ready");
  LOGI("APP", "ParaVario ready, free heap %u", ESP.getFreeHeap());
}

void loop() {
  const uint32_t now = millis();
  app.baro.update(now);
  app.gps.update(now);
  app.battery.update(now);

  switch (app.button.update(now)) {
    case ButtonEvent::Single:
      LOGD("BTN", "single press");
      actionToggleApproach();
      break;
    case ButtonEvent::Double:
      LOGD("BTN", "double press");
      actionToggleEmergency();
      break;
    case ButtonEvent::Long:
      LOGD("BTN", "long press");
      actionToggleMute();
      break;
    default: break;
  }

  if (now - lastToneMs >= kToneTickMs) {
    lastToneMs = now;
    audio::setVolumes(g_cfg.speechVolume, g_cfg.varioVolume);
    updateVarioTone();
  }

  if (now - lastTickMs >= kTickMs) {
    lastTickMs = now;
    handleFlightEvent(app.flight.update(now, app.baro, app.gps, app.nav));
    app.wind.update(app.nav);
    app.approach.update(app.nav, app.wind);
    app.emergency.update(app.nav, app.wind);
    app.announcer.update(app.nav, app.approach.active() || app.emergency.active());
    app.logger.update(app.nav, app.gps);
    checkGps(now);
    checkBattery(now);
    verifyResume();
    if (app.nav.flying && now - lastSaveMs >= kSaveIntervalMs) saveFlightState();
    if (dlog::kDebugBuild && now - lastTraceMs >= kTraceIntervalMs) {
      lastTraceMs = now;
      traceNav();
    }
  }

  app.ble.update(app.nav, app.gps, app.battery.percent(), app.logger.active());
  webui::update();
  dlog::pump();
  delay(1);
}
