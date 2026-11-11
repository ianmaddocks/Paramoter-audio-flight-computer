#pragma once
#include <stdint.h>

// Speaker volume control: one place that decides how loud speech and the vario tone are.
//
// The volume wanted is the pilot's setting, raised towards 100 % as the background noise rises
// (if a microphone is fitted and "Raise volume with background noise" is on). The volume
// actually applied then moves towards the wanted one at a limited rate (the "Fastest volume
// change" setting), so neither a sudden noise, a changed setting nor a glitch in the noise
// measurement can make the speaker jump to full volume in the pilot's ear. The audio output
// itself smooths the last step sample by sample, so changes are free of clicks.
namespace volume {

// Call regularly (every vario tone update). The first call applies the wanted volume at once.
void update(uint32_t nowMs);

// Volumes being applied now, 0..100 %.
uint8_t speechPct();
uint8_t varioPct();

// Volume wanted for a base setting at the current noise level (before rate limiting).
uint8_t wantedFor(uint8_t basePct);

}  // namespace volume
