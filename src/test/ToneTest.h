#pragma once

#if PV_TEST
// Software-level unit test of the vario tone decision (variosound::toneFor): the exact comparisons at
// the climb and sink thresholds, the tone pitch and beep rate formulas with their limits, and every
// condition that silences the tone. It uses its own Settings and NavState values, not the vario's, and
// touches no sensor or speaker. Runs when the test build is sent /api/sim?tonetest=1 and writes its
// result to the log: "TONET FAIL ..." for each wrong answer, then "TONET done pass=N fail=M".
void varioToneSelfTest();
#endif
