#pragma once
#include <stdint.h>

#include "nav/Nav.h"

// Spoken warnings that are not part of navigation: GPS fix gained ("GPS fix"), GPS lost in
// flight ("No GPS", after 5 s without a fix), and low battery (repeated every 5 minutes).
// On a vario without a GPS the GPS calls are never made.
namespace alerts {

void update(uint32_t nowMs, const NavState& nav, float batteryVolts);

// Test build: back to the power-on state.
void reset();

}  // namespace alerts
