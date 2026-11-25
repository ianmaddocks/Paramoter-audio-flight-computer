#pragma once
#include "comms/BleFeed.h"
#include "flight/Announcer.h"
#include "flight/Approach.h"
#include "flight/Emergency.h"
#include "flight/Flight.h"
#include "flight/Glide.h"
#include "flight/SafeArea.h"
#include "flight/HeightHistory.h"
#include "nav/Moving.h"
#include "flight/Wind.h"
#include "input/Button.h"
#include "logging/IgcLogger.h"
#include "nav/Nav.h"
#include "sensors/Baro.h"
#include "sensors/Battery.h"
#include "sensors/Gps.h"
#include "sensors/GpsData.h"

// The vario's state: every sensor, the flight logic built on them, and the outputs.
// One instance (`app`), owned by the main loop (app/App.cpp); the web UI reads it to show status.
// See the Architecture section of README.md for how the parts fit together.
struct App {
  // Inputs
  Baro baro;
  Gps gps;           // the receiver driver; an empty stub when no GPS is fitted
  GpsData gpsData;   // what it last reported (stays "no fix" without a GPS)
  Battery battery;
  Button button;

  // Flight logic. `nav` is the shared snapshot Flight::update() rebuilds every tick; the rest
  // read it.
  NavState nav;
  Flight flight;
  Glide glide;                  // glide ratio tile
  HeightHistory heightHistory;  // rolling height graph
  MovingState moving;           // there is a direction of travel to show (arrows), with hysteresis
  Wind wind;
  Announcer announcer;
  Approach approach;
  Emergency emergency;
  SafeArea safeArea;  // the safe flying areas drawn on the web page

  // Outputs
  BleFeed ble;
  IgcLogger logger;

  bool varioMuted = false;
  int flights = 0;  // takeoffs (and resumed flights) since power-up: the Flight time tile says Ready before the first, Landed after
};

extern App app;

// Called from setup() and loop() in main.cpp.
void appSetup();
void appLoop();

// The main-loop watchdog restarts the vario when the loop stops for 30 s. A firmware upload is handled inside one pass
// of the loop and a weak Wi-Fi link can stall it for tens of seconds, so the web UI relaxes the limit (120 s) for the
// length of an upload and puts it back afterwards. Returns the esp_err_t of the change (0 = done).
int appLoopWatchdog(bool relaxed);

// Pilot actions, shared by the push button and the web UI.
void actionToggleApproach();
void actionToggleEmergency();
void actionToggleMute();
void actionTellMe();
void actionResetFlight();

// First unmet pre-flight condition as text for the pilot, or nullptr when ready to fly.
const char* appNotReadyReason();

#if PV_TEST
// Test build: back to the state just after power-on, without restarting.
void testResetFlight();
void testFlushWind();  // forgets the circling samples gathered so far (the estimate is kept)
#endif
