#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_task_wdt.h>

#include "App.h"
#include "Log.h"
#include "Pins.h"
#include "Settings.h"
#include "Version.h"
#include "audio/Audio.h"
#include "audio/Speech.h"
#include "comms/WebUi.h"
#include "sensors/Mic.h"
#include "test/Sim.h"

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
uint32_t lastVolLogMs = 0;
uint32_t lastBatLogMs = 0;
const char* lastReadyWhy = "?";  // last ready-banner state written to the debug log
constexpr uint32_t kBatLogIntervalMs = 2000;
constexpr uint32_t kVolLogIntervalMs = 5000;
int lastBleClients = 0;

// Power-on sequence: a climb beep at the vario volume (so the pilot can judge the level),
// a pause, then "Ready". The normal vario tone is held off until it has finished.
constexpr uint32_t kStartBeepMs = 250;
constexpr uint32_t kStartWaitMs = 1000;
constexpr float kStartBeepClimbMs = 2.0f;  // beep as if climbing at this rate
enum class Startup : uint8_t { Beep, Wait, Done };
Startup startup = Startup::Beep;
uint32_t startupMs = 0;

void traceNav() {
#if PV_DEBUG
  const NavState& n = app.nav;
  LOGD("NAV", "fix=%d good=%d sat=%u hdop=%.1f %.6f,%.6f gs=%.1f trk=%.0f gpsAlt=%.1f", n.fix, n.gpsGood, n.sats,
       n.hdop, n.lat, n.lon, n.speedMs, n.trackDeg, n.gpsAltM);
  LOGD("NAV", "baro=%d p=%.0f t=%.1f std=%.1f msl=%.1f agl=%.1f vs=%+.2f fly=%d ft=%lu", n.baroOk,
       n.pressurePa, n.tempC, n.altStdM, n.altMslM, n.aglM, n.varioMs, n.flying,
       (unsigned long)n.flightTimeS);
  LOGD("BARO", "rawStd=%.3f filtStd=%.3f raw-filt=%.3f dt=%.4f settled=%d", app.baro.rawAltStdM(),
       app.baro.altStdM(), app.baro.rawAltStdM() - app.baro.altStdM(), app.baro.sampleDtS(),
       app.baro.settled());
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

// Debug log of what the tone generator was last told (mode change, or pitch moved 10 Hz), so the
// test runner can follow the vario sound from the log.
void logTone(const char* mode, float hz) {
#if PV_DEBUG
  static const char* lastMode = "";
  static float lastHz = 0;
  if (mode == lastMode && fabsf(hz - lastHz) < 10.0f) return;
  lastMode = mode;
  lastHz = hz;
  LOGD("TONE", "%s %.0f Hz", mode, hz);
#endif
}

// Debug builds: logs whenever the lowest-ever free memory has dropped by 1.5 kB or more, naming
// the part of the loop that just ran, so a memory-hungry operation shows up in the log.
void memCheck(const char* after) {
#if PV_DEBUG
  static uint32_t lastMin = UINT32_MAX;
  uint32_t m = ESP.getMinFreeHeap();
  if (lastMin == UINT32_MAX) lastMin = m;
  if (m + 1500 <= lastMin) {
    LOGW("MEM", "lowest free memory %u -> %u B after %s (free now %u)", (unsigned)lastMin, (unsigned)m,
         after, (unsigned)ESP.getFreeHeap());
    lastMin = m;
  }
#endif
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
    logTone("climb", hz);
  } else if (allowed && g_cfg.varioSinkAlarm && v <= g_cfg.sinkThreshold) {
    float hz = constrain(g_cfg.varioBaseHz * 0.5f + 20.0f * v, 150.0f, 1000.0f);
    audio::setTone(hz, 0, 0);
    logTone("sink", hz);
  } else {
    audio::setTone(0, 0, 0);
    logTone("off", 0);
  }
}

void updateStartup(uint32_t now) {
  if (startup == Startup::Done) return;
  if (startup == Startup::Beep) {
    if (!startupMs) {
      startupMs = now ? now : 1;
      float hz = constrain(g_cfg.varioBaseHz + g_cfg.varioHzPerMs * kStartBeepClimbMs, 200.0f, 2500.0f);
      audio::setTone(hz, 0, 0);  // continuous until stopped below
      logTone("startup", hz);
    } else if (now - startupMs >= kStartBeepMs) {
      audio::setTone(0, 0, 0);
      logTone("off", 0);
      startup = Startup::Wait;
      startupMs = now;
    }
  } else if (now - startupMs >= kStartWaitMs) {
    speech::say(Priority::Normal, "ready");
    startup = Startup::Done;
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

#if PV_TEST
// Test build: back to the state just after power-on, without the 25 s restart.
void testResetFlight() {
  app.approach.cancel(false);
  app.emergency.cancel(false);
  app.logger.stop();
  app.flight = Flight();
  app.wind = Wind();
  app.announcer = Announcer();
  app.approach = Approach();
  app.emergency = Emergency();
  app.varioMuted = false;
  hadFix = false;
  fixLostMs = 0;
  fixLostCalled = false;
  battLowSinceMs = 0;
  lastBattCallMs = 0;
  lastReadyWhy = "?";
  LOGI("SIM", "flight state reset");
}
#endif

// Restarts the vario if the main loop stops running for this long (a blocked network call, a
// stuck file operation): better a restart than an instrument that is silent until power-cycled.
constexpr uint32_t kLoopWatchdogS = 30;

void setup() {
  Serial.setTxBufferSize(2048);  // before begin(), which otherwise allocates 256 bytes
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block when no USB host is listening
  LOGI("APP", "ParaVario %s (%s, %s build), reset reason %d", PV_VERSION, PV_GIT,
       dlog::kDebugBuild ? "debug" : "release", (int)esp_reset_reason());
  settings::load();
  if (!LittleFS.begin(true)) LOGE("APP", "LittleFS mount failed");

  audio::begin();
  audio::setVolumes(g_cfg.speechVolume, g_cfg.varioVolume);
  mic::begin();  // before battery: the mic ADC DMA also samples the battery pin

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  app.baro.begin(Wire);
  app.gps.begin();
  app.battery.begin();
  app.button.begin(pins::BUTTON);
  app.ble.begin();
  webui::begin();
  // Disabled: do not resume a previously saved flight on power-up.
  // restoreFlightState();

  esp_task_wdt_init(kLoopWatchdogS, true);  // true: restart when it trips
  esp_task_wdt_add(nullptr);                // watch this (the loop) task
  // The ready beep and "Ready" are played from loop() by updateStartup().
  LOGI("APP", "ParaVario ready, free heap %u", ESP.getFreeHeap());
}

void loop() {
  const uint32_t now = millis();
  esp_task_wdt_reset();
  sim::update(now);
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
    uint8_t speechVol = mic::compensatedVolume(g_cfg.speechVolume);
    uint8_t varioVol = mic::compensatedVolume(g_cfg.varioVolume);
    audio::setVolumes(speechVol, varioVol);
    if (dlog::kDebugBuild && dlog::tracing() && now - lastVolLogMs >= kVolLogIntervalMs) {
      lastVolLogMs = now;
      LOGD("VOL", "noise %.1f dBFS, auto %s: speech %u%% (set %u%%, %+d), vario %u%% (set %u%%, %+d)",
           mic::noiseDb(), g_cfg.autoVolume ? "on" : "off", speechVol, g_cfg.speechVolume,
           (int)speechVol - (int)g_cfg.speechVolume, varioVol, g_cfg.varioVolume,
           (int)varioVol - (int)g_cfg.varioVolume);
    }
    updateStartup(now);
    if (startup == Startup::Done) updateVarioTone();
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
    if (dlog::kDebugBuild) {
      const char* why = webui::readyBlocker();
      if (why != lastReadyWhy) {
        lastReadyWhy = why;
        LOGD("RDY", "%s", why ? why : "ready");
      }
      if (dlog::tracing() && now - lastBatLogMs >= kBatLogIntervalMs) {
        lastBatLogMs = now;
        LOGD("BAT", "v=%.3f pct=%d min=%d learned=%d", app.battery.volts(), app.battery.percent(),
             app.battery.minutesLeftOrUnknown(), app.battery.learned());
      }
    }
    if (dlog::kDebugBuild && dlog::tracing() && now - lastTraceMs >= kTraceIntervalMs) {
      lastTraceMs = now;
      traceNav();
    }
  }

  memCheck("flight logic");
  app.ble.update(app.nav, app.gps, app.battery.percent(), app.logger.active());
  memCheck("bluetooth");
  webui::update();
  memCheck("web server");
  dlog::pump();
  delay(1);
}
