#pragma once
#include "comms/BleFeed.h"
#include "flight/Announcer.h"
#include "flight/Approach.h"
#include "flight/Emergency.h"
#include "flight/Flight.h"
#include "flight/Wind.h"
#include "input/Button.h"
#include "logging/IgcLogger.h"
#include "nav/Nav.h"
#include "sensors/Baro.h"
#include "sensors/Battery.h"
#include "sensors/Gps.h"

struct App {
  Baro baro;
  Gps gps;
  Battery battery;
  Button button;
  Flight flight;
  Wind wind;
  Announcer announcer;
  Approach approach;
  Emergency emergency;
  BleFeed ble;
  IgcLogger logger;
  NavState nav;
  bool varioMuted = false;
};

extern App app;

// Pilot actions, shared by the push button and the web UI.
void actionToggleApproach();
void actionToggleEmergency();
void actionToggleMute();
#if PV_TEST
void testResetFlight();
#endif
