#pragma once
#include "comms/BleFeed.h"
#include "flight/Announcer.h"
#include "flight/Approach.h"
#include "flight/Emergency.h"
#include "flight/Flight.h"
#include "flight/Glide.h"
#include "flight/HeightHistory.h"
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
  Wind wind;
  Announcer announcer;
  Approach approach;
  Emergency emergency;

  // Outputs
  BleFeed ble;
  IgcLogger logger;

  bool varioMuted = false;
};

extern App app;

// Called from setup() and loop() in main.cpp.
void appSetup();
void appLoop();

// Pilot actions, shared by the push button and the web UI.
void actionToggleApproach();
void actionToggleEmergency();
void actionToggleMute();

// First unmet pre-flight condition as text for the pilot, or nullptr when ready to fly.
const char* appNotReadyReason();

#if PV_TEST
// Test build: back to the state just after power-on, without restarting.
void testResetFlight();
void testFlushWind();  // forgets the circling samples gathered so far (the estimate is kept)
#endif
