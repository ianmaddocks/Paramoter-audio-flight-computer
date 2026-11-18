#pragma once
#include "flight/Wind.h"
#include "nav/Nav.h"

// "Tell me": the spoken status a single press of the button gives, so the pilot never has to look at the phone.
//   1. height above the landing zone, then (with a fix and a landing zone) the landing zone: clock direction relative to
//      the way you are travelling, and distance
//   2. the wind (where it comes from as a clock position, and its speed) when it is known
// The battery is not part of it: the pilot sees it on the page and hears the low-battery warnings.
// Two calls because one utterance holds at most 24 clips. A vario with no GPS gives only the height.
namespace status {
void tell(const NavState& nav, const Wind& wind, bool hasGps);
}
