// The main loop: reads the sensors, runs the flight logic, drives the outputs.
//
// Timing, fastest first:
//   every pass (about 1 ms)  sensors read, button, Bluetooth, web server, log output
//   every 20 ms              volume control and vario tone (variosound, volume)
//   every 100 ms             flight logic: altitude fusion, takeoff/landing, wind, guidance,
//                            announcements, flight log, alerts
// The sound itself is produced by a separate high-priority task (audio/Audio.cpp), and the
// microphone is analysed in another (sensors/Mic.cpp), so neither depends on how long a pass of
// this loop takes.
#include "App.h"

#include <Arduino.h>
#include <LittleFS.h>
#include <Wire.h>
#include <esp_task_wdt.h>

#include "Hardware.h"
#include "Log.h"
#include "Pins.h"
#include "Settings.h"
#include "Version.h"
#include "app/Alerts.h"
#include "app/Trace.h"
#include "app/VarioSound.h"
#include "audio/Audio.h"
#include "audio/Speech.h"
#include "audio/Volume.h"
#include "comms/WebUi.h"
#include "sensors/Mic.h"
#include "test/Sim.h"

using audio::Priority;

App app;

namespace {

constexpr uint32_t kTickMs = 100;       // flight logic
constexpr uint32_t kSoundTickMs = 20;   // volume and vario tone: short, for a responsive vario
constexpr uint32_t kSaveIntervalMs = 60000;
constexpr uint32_t kStaleRecordS = 15 * 60;
// Restarts the vario if the main loop stops running for this long (a blocked network call, a
// stuck file operation): better a restart than an instrument that is silent until power-cycled.
constexpr uint32_t kLoopWatchdogS = 30;

uint32_t lastTickMs = 0;
uint32_t lastSoundMs = 0;
uint32_t lastSaveMs = 0;
bool resumeUnverified = false;
uint32_t resumeSavedUtc = 0;

// ---------- flight state kept across a restart in the air ----------

void saveFlightState() {
  FlightRecord rec = {};
  app.flight.exportState(rec);
  app.wind.exportState(rec);
  rec.savedUtc = app.gpsData.utcEpoch;
  flightstore::save(rec);
  lastSaveMs = millis();
}

// Not called at present: resuming a saved flight at power-on is switched off (see appSetup).
[[maybe_unused]] void restoreFlightState() {
  FlightRecord rec;
  if (!flightstore::load(rec)) return;
  app.flight.importState(rec, millis());
  if (!rec.flying) return;
  app.wind.importState(rec);
  resumeUnverified = true;
  resumeSavedUtc = rec.savedUtc;
  LOGI("APP", "Restored in-flight state: %lu s flown, LZ %.6f,%.6f", (unsigned long)rec.flightTimeS, rec.lzLat,
       rec.lzLon);
}

// A restored flight is checked against GPS time once available: a stale record (device
// switched off in the air state and powered up much later) is dropped; otherwise the time
// spent rebooting is added to the flight time.
void verifyResume() {
  if (!resumeUnverified || !app.nav.flying) {
    resumeUnverified = false;
    return;
  }
  const uint32_t nowUtc = app.gpsData.utcEpoch;
  if (!nowUtc) return;
  resumeUnverified = false;
  if (!resumeSavedUtc) return;
  const uint32_t gap = nowUtc >= resumeSavedUtc ? nowUtc - resumeSavedUtc : UINT32_MAX;
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

// ---------- events ----------

void handleFlightEvent(FlightEvent ev) {
  if (ev == FlightEvent::Takeoff) {
    app.wind.onTakeoff(app.flight.takeoffCourseValid(), app.flight.takeoffCourseDeg());
    app.announcer.onTakeoff(app.nav);
    if (hw::hasGps()) app.logger.start(app.gpsData);  // an IGC log needs GPS time and position
    speech::say(Priority::High, "takeoff");
    saveFlightState();
    LOGI("APP", "Takeoff, course %.0f", app.flight.takeoffCourseDeg());
  } else if (ev == FlightEvent::Resumed) {
    app.announcer.onTakeoff(app.nav);
    if (hw::hasGps()) app.logger.start(app.gpsData);  // an IGC log needs GPS time and position
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

// With a GPS: single press = landing approach, double = emergency glide, long = mute.
// Without one there is no guidance to start, so every press mutes or unmutes the vario.
void handleButton(ButtonEvent ev) {
  if (ev == ButtonEvent::None) return;
  LOGD("BTN", "%s press", ev == ButtonEvent::Single ? "single" : ev == ButtonEvent::Double ? "double" : "long");
  if (!hw::hasGps() || ev == ButtonEvent::Long) actionToggleMute();
  else if (ev == ButtonEvent::Single) actionToggleApproach();
  else if (ev == ButtonEvent::Double) actionToggleEmergency();
}

void readSensors(uint32_t now) {
  sim::update(now);
  app.baro.update(now);
  app.gps.update(now);  // also keeps the "is a GPS fitted" detection going
  if (hw::hasGps()) app.gps.read(now, app.gpsData);
  else app.gpsData = GpsData();
  app.battery.update(now);
}

void flightTick(uint32_t now) {
  handleFlightEvent(app.flight.update(now, app.baro, app.gpsData, app.nav));
  if (hw::hasGps()) {  // everything here needs position
    app.wind.update(app.nav);
    app.approach.update(app.nav, app.wind);
    app.emergency.update(app.nav, app.wind);
    app.logger.update(app.nav, app.gpsData);
    verifyResume();
  }
  app.announcer.update(app.nav, app.approach.active() || app.emergency.active());
  app.glide.update(now, app.nav);
  app.heightHistory.update(now, app.nav.aglM);
  alerts::update(now, app.nav, app.battery.volts());
  if (app.nav.flying && now - lastSaveMs >= kSaveIntervalMs) saveFlightState();
  trace::update(now);
}

}  // namespace

// ---------- pilot actions ----------

void actionToggleApproach() {
  if (!hw::hasGps() || app.emergency.active()) return;
  if (app.approach.active()) app.approach.cancel(true);
  else app.approach.start(app.nav, app.wind);
}

void actionToggleEmergency() {
  if (!hw::hasGps()) return;
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

const char* appNotReadyReason() {
  const NavState& n = app.nav;
  if (hw::gpsState() == hw::GpsState::Unknown) return "Looking for GPS";
  if (hw::hasGps()) {
    if (!app.gpsData.link) return "GPS not responding";
    if (!n.fix || n.sats < g_cfg.readyMinSats) return "Waiting for satellites";
    if (n.hdop > g_cfg.readyMaxHdop) return "GPS accuracy low";
    if (!n.gpsGood) return "GPS position unstable";
  }
  if (!n.baroOk) return "Barometer not working";
  if (!app.baro.settled()) return "Vario settling";
  if (hw::hasGps() && !n.lzValid) return "Landing zone not set";
  return nullptr;
}

#if PV_TEST
// The wind estimator keeps 60 s of ground-velocity samples; a test that wants a fresh collection starts with this
// (onTakeoff without a valid course clears the samples and nothing else).
void testFlushWind() { app.wind.onTakeoff(false, 0); }

void testResetFlight() {
  app.approach.cancel(false);
  app.emergency.cancel(false);
  app.logger.stop();
  app.flight = Flight();
  app.wind = Wind();
  app.announcer = Announcer();
  app.approach = Approach();
  app.emergency = Emergency();
  app.glide = Glide();
  app.heightHistory = HeightHistory();
  app.varioMuted = false;
  alerts::reset();
  trace::reset();
  LOGI("SIM", "flight state reset");
}
#endif

// ---------- setup and loop ----------

void appSetup() {
  Serial.setTxBufferSize(2048);  // before begin(), which otherwise allocates 256 bytes
  Serial.begin(115200);
  Serial.setTxTimeoutMs(0);  // never block when no USB host is listening
  LOGI("APP", "ParaVario %s (%s, %s build), reset reason %d", PV_VERSION, PV_GIT,
       dlog::kDebugBuild ? "debug" : "release", (int)esp_reset_reason());
  LOGI("APP", "Hardware: microphone %s; GPS detected at start-up (setting \"GPS module\")",
       PV_HAS_MIC ? "fitted" : "not fitted");
  settings::load();
  if (!LittleFS.begin(true)) LOGE("APP", "LittleFS mount failed");

  audio::begin();
  volume::update(millis());  // sets the starting volume before the first sound
  mic::begin();              // before the battery: the mic's ADC sampler also reads the battery pin

  Wire.begin(pins::I2C_SDA, pins::I2C_SCL, 400000);
  app.baro.begin(Wire);
  app.gps.begin();
  app.battery.begin();
  app.button.begin(pins::BUTTON);
  app.ble.begin();
  webui::begin();
  // Resuming a flight saved before a restart is switched off: restoreFlightState() is not called.

  esp_task_wdt_init(kLoopWatchdogS, true);  // true: restart when it trips
  esp_task_wdt_add(nullptr);                // watch this (the loop) task
  // The ready beep and "Ready" are played from the loop by variosound.
  LOGI("APP", "ParaVario ready, free heap %u", ESP.getFreeHeap());
}

void appLoop() {
  const uint32_t now = millis();
  esp_task_wdt_reset();

  readSensors(now);
  handleButton(app.button.update(now));

  if (now - lastSoundMs >= kSoundTickMs) {
    lastSoundMs = now;
    volume::update(now);
    variosound::update(now, app.nav, app.varioMuted);
  }
  if (now - lastTickMs >= kTickMs) {
    lastTickMs = now;
    flightTick(now);
  }
  trace::memCheck("flight logic");

  app.ble.update(app.nav, app.gpsData, app.battery.percent(), app.logger.active());
  trace::memCheck("bluetooth");
  webui::update();
  trace::memCheck("web server");
  dlog::pump();
  delay(1);
}
