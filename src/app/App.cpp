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
#include "app/Status.h"
#include "flight/Resume.h"
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
constexpr uint32_t kResumeWindowMs = 180000;  // a saved flight not resumed or refused within this long after power-up is dropped
// Restarts the vario if the main loop stops running for this long (a blocked network call, a
// stuck file operation): better a restart than an instrument that is silent until power-cycled.
constexpr uint32_t kLoopWatchdogS = 30;

uint32_t lastTickMs = 0;
uint32_t lastSoundMs = 0;
uint32_t lastSaveMs = 0;
bool resumePending = false;  // a flight saved as "flying" is waiting for the first trusted GPS fix to be judged
FlightRecord resumeRec = {};
uint32_t resumeDeadlineMs = 0;

// ---------- flight state kept across a restart in the air ----------

void saveFlightState() {
  FlightRecord rec = {};
  app.flight.exportState(rec);
  app.wind.exportState(rec);
  rec.savedUtc = app.gpsData.utcEpoch;
  rec.posValid = app.nav.fix && gpsCoordsValid(app.nav.lat, app.nav.lon);
  rec.lastLat = rec.posValid ? app.nav.lat : 0.0;
  rec.lastLon = rec.posValid ? app.nav.lon : 0.0;
  flightstore::save(rec);
  lastSaveMs = millis();
}

// A flight saved as "flying" (a restart in the air, or the vario switched off and on again: see the battery advice) is
// loaded at power-up but NOT believed. The vario stays on the ground state until the first trusted GPS fix with a time,
// where resumeDecide() (flight/Resume.h) judges it once: recent enough, in a possible place, and moving at flying
// speed right now. Only then is the flight resumed (LZ, ground reference, wind, flight time). Otherwise the record is
// dropped, so the vario can never come up convinced it is flying when it is not.
void loadResume() {
  resumePending = false;
  FlightRecord rec;
  if (g_cfg.resumeMaxMin <= 0 || !flightstore::load(rec) || !rec.flying) return;
  resumeRec = rec;
  resumePending = true;
  resumeDeadlineMs = millis() + kResumeWindowMs;
  LOGI("APP", "A flight saved in the air is waiting to be resumed (%lu s flown, LZ %.6f,%.6f)", (unsigned long)rec.flightTimeS,
       rec.lzLat, rec.lzLon);
}

void dropResume(const char* why) {
  resumePending = false;
  LOGI("APP", "Saved flight not resumed: %s", why);
  saveFlightState();  // writes the current (not flying) state over the old record
}

void updateResume(uint32_t now) {
  if (!resumePending) return;
  if (app.nav.flying) {  // took off normally meanwhile: the record is stale
    dropResume("a new takeoff was detected");
    return;
  }
  if ((int32_t)(now - resumeDeadlineMs) >= 0) {
    dropResume("no GPS position and time within 3 minutes of power-up");
    return;
  }
  if (!app.gpsData.fix || !app.nav.speedOk || !app.gpsData.utcEpoch) return;  // not yet: judged at the first trusted fix
  ResumeParams p;
  p.maxAgeS = (uint32_t)g_cfg.resumeMaxMin * 60;
  p.minSpeedMs = fmaxf(g_cfg.takeoffSpeedMs, 5.0f);
  p.saveIntervalS = kSaveIntervalMs / 1000.0f;
  uint32_t age = 0;
  const ResumeVerdict v = resumeDecide(resumeRec, app.gpsData.utcEpoch, app.nav.lat, app.nav.lon, app.nav.speedMs, p, &age);
  if (v != ResumeVerdict::Resume) {
    dropResume(resumeVerdictName(v));
    return;
  }
  resumePending = false;
  app.flight.importState(resumeRec, now);
  app.wind.importState(resumeRec);
  app.flight.addFlightTime(age);  // the time it was off counts as flown
  LOGI("APP", "Resuming the flight: saved %lu s ago, %lu s flown, LZ %.6f,%.6f", (unsigned long)age,
       (unsigned long)resumeRec.flightTimeS, resumeRec.lzLat, resumeRec.lzLon);
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

// The button. A click tone confirms every press and a rising cue says "keep holding". A single click only ever
// TELLS (status): it never starts or cancels guidance, so an accidental touch cannot do any harm. Starting and
// cancelling guidance are the deliberate gestures.
//
//   with a GPS     single = tell me (height, landing zone, wind, battery)
//                  double = landing approach, start or cancel
//                  hold 1.5 s = emergency glide, start or cancel
//                  triple = mute or unmute the vario
//   without a GPS  single = tell me (height, battery); double, triple or hold = mute or unmute
void handleButton(ButtonEvent ev) {
  if (ev == ButtonEvent::None) return;
  if (ev == ButtonEvent::Press) {
    LOGD("BTN", "click");
    audio::cue(1800.0f, 15);
    return;
  }
  if (ev == ButtonEvent::HoldCue) {
    LOGD("BTN", "hold cue");
    audio::cue(1200.0f, 120);
    return;
  }
  LOGD("BTN", "%s", ev == ButtonEvent::Single ? "single" : ev == ButtonEvent::Double ? "double"
                                                     : ev == ButtonEvent::Triple ? "triple" : "hold");
  if (ev == ButtonEvent::Single) {
    actionTellMe();
  } else if (!hw::hasGps() || ev == ButtonEvent::Triple) {
    actionToggleMute();
  } else if (ev == ButtonEvent::Double) {
    actionToggleApproach();
  } else if (ev == ButtonEvent::Hold) {
    actionToggleEmergency();
  }
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
    updateResume(now);
  }
  app.announcer.update(app.nav, app.approach.active() || app.emergency.active());
  app.glide.update(now, app.nav);
  app.heightHistory.update(now, app.nav.aglM);
  alerts::update(now, app.nav, app.battery.volts(), app.battery.minutesLeftOrUnknown());
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

void actionTellMe() {
  status::tell(app.nav, app.wind, app.battery.percent(), app.battery.minutesLeftOrUnknown(), hw::hasGps());
}

// The way out of a vario that thinks it is flying when it is not (also available as a button on the Logs & system tab):
// forget the flight, any guidance, any saved flight waiting to resume, and write that down.
void actionResetFlight() {
  resumePending = false;
  app.flight.abandonFlight();
  app.approach.cancel(false);
  app.emergency.cancel(false);
  app.logger.stop();
  saveFlightState();
  LOGI("APP", "Flight state reset by the pilot");
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
  loadResume();  // judged later, at the first trusted GPS fix (updateResume)

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
#if PV_TEST
  handleButton((ButtonEvent)sim::takeButton());  // a press injected by a test (button=single|double|triple|hold|press|holdcue)
#endif

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
