#pragma once
#include "flight/Wind.h"
#include "nav/Nav.h"

// "Tell me": the spoken status a single press of the button gives, so the pilot never has to look at the phone.
//   1. height above the landing zone, then (with a fix and a landing zone) the landing zone: clock direction relative to
//      the way you are travelling, and distance
//   2. the wind (where it comes from as a clock position, and its speed) when it is known, then the battery: minutes left
//      when the vario has learned them, otherwise the percentage
// Two calls because one utterance holds at most 24 clips. A vario with no GPS gives only the height and the battery.
namespace status {
void tell(const NavState& nav, const Wind& wind, int batteryPct, int batteryMinutes /* -1 unknown */, bool hasGps);
}
