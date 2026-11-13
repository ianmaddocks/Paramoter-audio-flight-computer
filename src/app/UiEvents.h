#pragma once
#include <stdint.h>

// Evidence for what the web page was told to show. The page decides what to display from the vario's status with a few
// simple tests (a direction of travel exists when there is a fix and the ground speed is at least 1.5 m/s: then the compass,
// the landing zone arrow and the windsock appear; the GPS tile says strong / good / ok / poor / none from the HDOP; the Actions
// card follows flying; the alert strip follows the barometer, the fix and the battery). This runs the same tests on the
// same values and writes one Info line ("UI") each time one of them changes, with the numbers that made it change, so a tile
// that comes and goes can be matched to the cause. Present in every build.
namespace uievents {

// Call once per flight-logic tick, after the navigation state has been updated.
void update(uint32_t nowMs);

void reset();

}  // namespace uievents
