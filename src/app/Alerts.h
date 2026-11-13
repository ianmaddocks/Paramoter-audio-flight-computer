#pragma once
#include <stdint.h>

#include "nav/Nav.h"

// Spoken warnings that are not part of navigation: GPS fix gained ("GPS fix"), GPS lost in
// flight ("No GPS", after 5 s without a fix), the barometer failing in flight ("Barometer failed", then "Barometer back"),
// low battery (repeated every 5 minutes, with the minutes left and the distance to the landing zone, and advice to switch
// the vario off until it is needed), critical battery and the battery reading lost in flight (urgent, every minute).
// On a vario without a GPS the GPS calls are never made.
namespace alerts {

// batteryMinutes: minutes of flying left as the battery learner knows them, -1 when it does not.
void update(uint32_t nowMs, const NavState& nav, float batteryVolts, int batteryMinutes = -1);

// Test build: back to the power-on state.
void reset();

}  // namespace alerts
