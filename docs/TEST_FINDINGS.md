# Test findings: suspected defects and design limits

Found while extending the test suite. **None of these has been fixed**: the tests describe today's behaviour
(or avoid the case) and each item has a proposed fix for you to accept or reject. "Observed" means a test
ran and saw it; "read" means it follows from the code and is not yet exercised.

## F1 - Level flight with no GPS is taken for a landing (observed)
`Flight.cpp`, landing detection. With no fix the vario treats `|vertical speed| < 0.3 m/s` as "stationary"
and declares **Landing** after twice the landing time (default 2 x 30 s = 60 s), whatever the height.
Observed in the test build (2 x 5 s): a GPS dropout of 14 s in level flight at 60 m above the ground ended
the flight ("landing candidate: gs 0.0 m/s, agl 60.0 m", then not flying). Consequences: tones set to "only
when flying" fall silent, the IGC log closes, and the approach and emergency functions refuse.
*Proposed fix:* with no fix, also require the height above the ground level to be small (for example under
10 m, or under the takeoff gain) before a landing can be declared.

## F2 - Wind samples from before a wind change bias the next fit (observed, by design)
`Wind.cpp` keeps 60 s of ground-velocity samples and fits all of them. Flying straight in one wind and then
circling in another, a fit made inside the 60 s mixes both: in the test two stray samples moved the
estimate by 1.1 to 1.8 m/s. Each accepted fit is then averaged 50/50 with the previous estimate, so a real
change of wind takes three fits (about a minute of circling) to be 87 % followed. *Proposed fix (optional):*
reject fits whose samples show a step (compare the fit on the older and newer halves), or weight newer
samples more. Not urgent.

## F3 - RPM drops to zero during long speech (read)
`Mic.cpp` pauses RPM updates while the speaker talks (the speaker would pollute the spectrum) and clears
the reading after 1.5 s without an update, so any announcement longer than about 1.5 s shows `off`.
*Proposed fix:* hold the last good RPM for the length of the speech plus 1.5 s.

## F4 - Rapid RPM changes are followed without a spike filter (observed)
A jump of more than 20 % is accepted at once (only a confidence gate protects against nonsense). With a
clean synthetic signal this is correct (the test alternates 2000 and 4000 rpm and never sees a value outside
that range); with real noise a single bad window could show a wrong value for one 0.25 s step. Low risk.

## F5 - Sensor faults that are not detected (read; Phase 4 will exercise them)
* Barometer: no jump or stuck-value detection (only the 20000-110000 Pa range check and a 2 s timeout).
* GPS: no latitude/longitude range validation.
* Battery: an ADC reading of 0 V is shown as "USB / no battery" and suppresses the low-battery warning;
  there is no critical-battery threshold.

## F6 - "Steer right/left" is spoken during every turn the vario has just called (observed)
`Approach.cpp`, `steerTo`: the steering call comes when the track has been more than 30 degrees off the leg
heading for 4 s. After "turn right now" a pilot needs a moment to react and then 4 to 8 s to turn 90
degrees, so the track is still more than 30 degrees off 4 s after the turn call and "steer right" is spoken
in the middle of the turn it asked for. In the full circuit test (reaction and turn about 5 s) it happened
after four of the five turn calls. *Proposed fix:* start the 4 s timer only after a grace period following a
turn call (about 8 s), or do not call while the heading error is shrinking.

## F7 - Barometer faults other than "no data" are not detected (observed by `fault_baro`)
A frozen value (stuck sensor) reads as level flight: vario 0, no banner, silent. One wild sample (30 m) moves
the filter and can sound a brief false tone before it calms down; noise raises the vario reading's wobble.
*Proposed fix:* a stuck-value check (identical raw pressure for several seconds while the GPS shows
climb/descent) and a spike limit (reject a sample more than about 20 m from the prediction unless the next
one agrees).

## F8 - A weak GPS fix in flight (see `fault_gps`)
Result recorded by the test run (flight kept or ended). If the flight is ended, a weak fix (4 satellites or a
high HDOP) zeroes the ground speed and the landing logic then declares a landing after the landing time.
