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

// Where the Help tab gets the guides: the docs folder of the project on GitHub, always the latest
// version. The page fetches the Markdown from PV_HELP_RAW_URL (GitHub's raw file server) and shows
// it itself, as plain content; PV_HELP_WEB_URL is the same folder as GitHub shows it, used as the
// link address and for "Open on GitHub". Both must end in a slash. For a fork, change them with
// -DPV_HELP_RAW_URL=\"https://raw.githubusercontent.com/you/fork/main/docs/\" and
// -DPV_HELP_WEB_URL=\"https://github.com/you/fork/blob/main/docs/\".
#ifndef PV_HELP_RAW_URL
#define PV_HELP_RAW_URL "https://raw.githubusercontent.com/ianmaddocks/Paramoter-audio-flight-computer/main/docs/"
#endif
#ifndef PV_HELP_WEB_URL
#define PV_HELP_WEB_URL "https://github.com/ianmaddocks/Paramoter-audio-flight-computer/blob/main/docs/"
#endif

// What the boot log, web page and status report as the version, e.g. 20261111-131219-advanced.
#define PV_VERSION PV_STAMP "-" PV_VARIANT
