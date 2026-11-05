#pragma once

// PV_VERSION (build time, yyyymmdd-hhmmss) and PV_GIT come from BuildVersion.h, generated in
// the build directory by tools/pio_ota.py on every build.
#if __has_include("BuildVersion.h")
#include "BuildVersion.h"
#endif
#ifndef PV_VERSION
#define PV_VERSION "unknown"
#endif
#ifndef PV_GIT
#define PV_GIT ""
#endif
