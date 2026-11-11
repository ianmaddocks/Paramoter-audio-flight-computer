#pragma once
#include "Hardware.h"

// PV_STAMP (build time, yyyymmdd-hhmmss) and PV_GIT come from BuildVersion.h, generated in the
// build directory by tools/pio_ota.py on every build.
#if __has_include("BuildVersion.h")
#include "BuildVersion.h"
#endif
#ifndef PV_STAMP
#define PV_STAMP "unknown"
#endif
#ifndef PV_GIT
#define PV_GIT ""
#endif

// Which hardware variant this firmware is for, from the PV_HAS_GPS / PV_HAS_MIC build flags:
//   advanced   GPS and microphone fitted      (-e release, debug, test)
//   basic      neither: barometer and speaker (-e basic)
// A build with only one of the two parts is labelled "basic+gps" or "basic+mic".
#if PV_HAS_GPS && PV_HAS_MIC
#define PV_VARIANT "advanced"
#elif !PV_HAS_GPS && !PV_HAS_MIC
#define PV_VARIANT "basic"
#elif PV_HAS_GPS
#define PV_VARIANT "basic+gps"
#else
#define PV_VARIANT "basic+mic"
#endif

// Where the Help tab and the "Help" links on the settings page open the guides: the docs folder
// of the project on GitHub, always the latest version. Change it for a fork with
// -DPV_HELP_URL=\"https://github.com/you/your-fork/blob/main/docs/\" (must end in a slash).
#ifndef PV_HELP_URL
#define PV_HELP_URL "https://github.com/ianmaddocks/Paramoter-audio-flight-computer/blob/main/docs/"
#endif

// What the boot log, web page and status report as the version, e.g. 20261111-131219-advanced.
#define PV_VERSION PV_STAMP "-" PV_VARIANT
