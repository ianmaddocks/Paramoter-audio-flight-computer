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

// What the boot log, web page and status report as the version, e.g. 20261111-131219. One firmware
// serves every vario; whether a GPS is fitted is found out at start-up (see Hardware.h).
#define PV_VERSION PV_STAMP
