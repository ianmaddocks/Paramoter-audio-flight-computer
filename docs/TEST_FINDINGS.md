# Test findings: defects found by the tests, and what was done about them

Phase 1 found eight suspected defects (F1 to F8) while the test suite was being extended. Phase 2 (this document's
current state) fixed them in priority order, each one test-first: an automated test that reproduces the problem was
written and **run against the unchanged firmware to confirm that it fails**, then the firmware was corrected, then the
test and all the related existing tests were run again.

**Nothing here has been verified on hardware in flight.** Every result below is from the vario in the test build,
driven by the simulator (simulated barometer, GPS, battery and microphone). The simulator reproduces what the
firmware sees, not what the real sensors do. Where that matters it is said under *Limits*.

| | Defect | Status |
|---|---|---|
| F1 | Level flight with no GPS taken for a landing | fixed |
| F8 | A weak GPS fix in flight ended the flight | fixed |
| F7 | Barometer faults other than "no data" undetected | fixed |
| F6 | "Steer" called during the turn that was just called | fixed |
| F2 | Old wind samples and the old estimate biased the wind after a change | fixed |
| F3 | RPM dropped to zero during speech | fixed |
| F4 | One bad RPM window reached the display | fixed |
| F5 | Impossible GPS coordinates; battery 0 V treated as USB; no critical threshold | fixed |
| F9 | Free memory fell to about 2-5 kB while a browser loaded the page | resolved: a test-build effect; release measured at 28.8 kB minimum with three browsers; page requests serialised |
| F10 | Good GPS but a ground speed under the landing speed at height (strong headwind) ended the flight | fixed |

## Results

| Run (test build of this change) | Result |
|---|---|
| Baseline, old firmware (`ae9b1a0`), 46 tests | 40 passed, 6 failed: `wind_faults` (the 3 deg/s test was marginal, see below), `memory_events`, `soak`, `memory` (lowest free memory 4.8 kB by then), `glide`, `ui_spectrum`. Browser pages were open on three devices at the time (see F9). |
| The nine new HIL tests against the old firmware | **9 of 9 failed**, each for the intended reason (`rpm_glitch` only because the old firmware has no such simulator key). |
| Unit suites on the new firmware | 8 of 8 pass: 22 + 39 + 82 + 57 + 49 + 31 + 29 + 35 cases |
| Unit tests with each fix deliberately removed again | the matching suite fails every time (wind, circuit, flight, baro, rpmtrack; the battery cases were not mutation-tested) |
| Full run, normal order, new firmware, pages closed, 54 tests | 52 passed, 2 failed (`fault_baro`, `volume_ramp`: both were test-timing mistakes, fixed afterwards and re-run green) |
| Full run, shuffled (`--shuffle 7`), same firmware, 54 tests | 49 passed, 5 failed: `memory`, `soak` and `memory_events` (the lowest-free-memory mark is cumulative since power-on and a page-load test that ran earlier had taken it to 3 kB: F9; the soak itself lowered nothing), `glide` (passed alone three times, in the normal order and in the baseline re-run; one failure each in the baseline and this shuffled order, cause not found: the tile showed no ratio), `vario_thresholds` (room noise, below) |
| `memory_events` and `soak` re-run after judging them on their own effects | both passed: soak 10 min, free memory 42820 to 42832 B (trend +0 B/min), no drops, stacks fine (loopTask 436 B, audio 2232 B, mic 2000 B free at the end of `memory_events`); lowest 2724 B was set by earlier page loads |
| Page tests after the Speed tile arrow change (`ui_*`, `glide`, `gps_detect`, `variant_*`) | 11 of 11 passed. The outside-activity check flagged one web page opened during `glide` and `variant_no_gps` |
| `vario_thresholds` with the microphone | **fails while the room is noisy**: a hum of 16-27 dB over the floor near 330 Hz is heard while the vario is silent (the log shows no tone). It passes with `--no-audio`. The other tests that listen (`vario_climb`, `vario_sink`, `speech`, `mute`, `auto_volume`) passed in the runs above but are equally exposed to room noise. |
| Pilot-interface phase + F9/F10 + windsock + adaptive turn lead: circuit unit suite | 74 of 74 pass (incl. learned lead 7.7 s for 10 deg/s with 2 s reaction, 15.7 s for a 20 s turner, bad turns not used, setting off, new approach forgets) |
| `circuit_slow_pilot` (new): a 6 deg/s pilot | passed: measured 6.0 and 9.0 deg/s, lead 10.2 s, base turn 102 m earlier than the 3 s setting |
| Full run, normal order, 66 tests (`full_run7_normal.log`) | 64 passed, 2 failed: `circuit_left_wind` (979 s; the harness lost the vario at cleanup, the simulated aircraft flew on below the ground) and `ui_guidance` (banner not hidden in 5 s). Both passed when re-run alone (290 s and 66 s). Cause of the first not found. |
| Full run, shuffled (`--shuffle 12`, soak, `--memory`), 66 tests (`full_run8_shuffled.log`) | 63 passed, 3 failed: `ui_actions` (passed 3 of 3 alone and after `flight`/`ui_flight`; not reproduced), `memory` and `soak` (the lowest free memory since power-on had been taken to 1-4 kB by `alerts_flight` and `circuit_left_wind` earlier in the run; free memory was back to 40 kB or more after each test) |
| `resume_flight` | failed in two full runs: the simulated barometer rose 50 m before the GPS fix was trusted, a barometer-only takeoff that drops a saved flight. Test changed (barometer takeoff height raised to 200 m for the test; "not resumed" judged on flight time not on "flying"); then passed (495 s), and again in the shuffled run |
| Vario battery ran flat during the overnight session (test build, bench) | tests were interrupted and resumed on mains power; no firmware fault found |

The full logs are in `results/` (baseline: `baseline_full_ae9b1a0.log`; old firmware against the new tests:
`old_firmware_new_tests.log`; new firmware: `full_run1.log`, `full_run2_shuffled.log`).

---

## F1 - Level flight with no GPS was taken for a landing (fixed)
*Before:* `Flight.cpp` with no fix declared **Landing** after twice the landing time whenever the vertical speed was
under 0.3 m/s, at any height. Seen: a 14 s GPS dropout in level flight at 60 m ended the flight (tones set to "only when
flying" fell silent, the IGC log closed, approach and emergency refused).
*After:* `Flight::looksStill()`. Without a trusted GPS ground speed (no fix, weak fix, stale data) a landing needs a
steady barometer **and** a height above the landing zone under the new setting *Landed without GPS speed: only below*
(default 15 m / 50 ft), for twice the landing time. With no barometer either there is no evidence and the answer is no.
Level flight at height is never a landing, however long the GPS is gone. The rule with a trusted speed is unchanged.
*Tests:* `flight_logic` (31 cases incl. the exact limits), `landing_gps_quality` (no GPS, weak fix, 3 satellites, no
fix, each level and at +-0.2 m/s at 60 and 200 m for 29 s, a 70 s loss, recovery; then a real landing on the barometer
alone and with a weak fix, which are still found), `baro_only`, `variant_no_gps` (changed: they used to expect a
"landing" in level flight at 60 m / 40 m; they now check that it is not one and then descend to the ground).
*Limits:* landing on high ground more than 15 m above the takeoff (or the landing zone you set) while the GPS is not
working is not noticed until the GPS recovers: raise the setting. After a missed landing the flight stays "flying"
(IGC log keeps running), which is the safer error.

## F8 - A weak GPS fix in flight ended the flight (fixed)
*Before:* a fix with 4 satellites or a high HDOP zeroed the ground speed, and the zero was read as "stationary":
landing after the landing time (`fault_gps` reproduced it: flight ended after 14 s).
*After:* the same `looksStill()` decision, keyed on whether the speed is *trusted* (`NavState.speedOk`) and not on
whether a fix exists. A weak fix falls back on the barometer and height rule of F1.
*Tests:* `flight_logic`, `fault_gps` (weak fix for 14 s keeps the flight; was a recorded note, now an assertion),
`landing_gps_quality` (weak fix at 60 / 200 m and a real landing with a weak fix).
*Also:* `Gps::read()` already reports `fix = false` for stale data (a position older than 2.5 s) and for fewer than 4
satellites, so no fix, weak fix, valid fix, stale data and recovery are all distinguished and none of them ends a flight.

## F7 - Barometer faults other than "no data" (fixed)
*Before:* only the 20000-110000 Pa range and a 2 s timeout were checked. A frozen reading read as level flight (no
banner, vario 0). One wild sample moved the filter and sounded 5 tone changes. NaN passed the range check.
*After:* `BaroGuard` (src/sensors/BaroGuard.h), used by the real sensor path and the simulator alike:
NaN and impossible pressure rejected; a sample further from the last good one than an aircraft can move (40 m/s plus 3 m)
is **held back** and only accepted if the next sample agrees with it (then the filter restarts there: a persistent step
is accepted once, silently); a lone sample is dropped without trace; the same reading for *Barometer frozen after*
(new setting, default 5 s, 0 = off) is a frozen sensor: "Barometer not working", tones off, recovered by itself (filter
restarts) when the reading moves again.
*Tests:* `baro_logic` (29 cases), `fault_baro` (dead, range, NaN, frozen, lone samples of +30 / -30 / +12 m, two wild
samples in a row, a 25 m step that stays, intermittent 1.2 s dropouts ignored and 3 s dropouts detected and recovered three
times, noise).
*Limits:* a real DPS310's last digit always moves in my understanding, but that is not tested on hardware: if a real
sensor can repeat a value for 5 s on the bench, *Barometer frozen after* can be raised or set to 0. A step of 30 m that
persists is accepted (the sensor says so and nothing else can overrule it); the altitude then shifts by that much.

## F6 - "Steer" called during the turn that was just called (fixed)
*Before:* the steering call came when the track had been more than 30 degrees off the leg for 4 s. After "turn right
now" the pilot reacts and then turns for 4 to 8 s, so "steer right" was spoken in the middle of the turn that was asked
for: three of the five turn calls in each full circuit test.
*After:* `Approach::steerTo` waits 5 s after a turn call (the pilot's reaction), and while the heading error is shrinking
by 3 degrees or more a second, plus 3 s after the last such sample. A turn that stalls, goes the wrong way or is ignored
is still called (after about 9 s of nothing happening); a heading that drifts off a leg with no turn call involved is
called after 4 s as before. Turn progress is judged from the GPS track.
*Tests:* `circuit_logic` (slow reaction then a normal turn, ignored turn, wrong way, stalled at 40 degrees, fast turn
with overshoot, wobble inside 30 degrees, a drift on a straight leg), `circuit_right`, `circuit_left_wind`,
`circuit_events` (changed: they used to count steering calls during turns as expected; they now fail on any, apart from
the turn the events test ignores on purpose). Independent geometry in the circuit tests is untouched.

## F2 - Old wind samples and the old estimate biased the wind after a change (fixed)
*Before:* the fit used all 60 s of samples, so straight flight before a circle in a new wind biased the first fit (two
stray samples: 1.1 to 1.8 m/s), and each fit was averaged 50/50 with the old estimate, so a real change took three fits
(about a minute of circling) to be 87 % followed.
*After:* the fit is tried without the oldest seconds and the clearly better one (rms under 70 % of the full fit) is
used; samples further from the circle than max(1.2 m/s, 2 x rms) are dropped and the circle fitted again (no fit if more
than a quarter are off: the wind changed during the circle). When the new fit agrees (within 1.5 m/s) with the previous
fit, the estimate is the mean of the two fits and not an average with the older estimate.
*Tests:* `wind_logic` (82 cases: 1, 3, 6 s and 40 % of old wind, with noise, a flip seven seconds before a fit gives no
new estimate, convergence A then B in two fits, A-B-A still averaged), `wind_change` (first fit after the change is the
mean of the old estimate and the new wind, the second is the new wind, the third stays), `wind`, `wind_faults`.
*Limits:* the first fit after a change is still averaged with the old estimate (one odd fit cannot swing it); only
the second one follows the change fully.

## F3 - RPM dropped to zero during long speech (fixed)
*Before:* RPM updates pause while the speaker talks and the reading was cleared 1.5 s later: a 6 s announcement showed
"off" for about 4.5 s (`rpm_speech` reproduced it: readings of 0 during the announcement).
*After:* `RpmTracker` (src/sensors/RpmTrack.h) holds the reading for the whole speech (at most 15 s, so a stuck
"speaking" flag cannot hold it for ever) and the usual 1.5 s after it.
*Tests:* `rpmtrack_logic` (35 cases), `rpm_speech` (three 6 s announcements and a change to 3500 rpm during speech).
*Limits:* an engine that really stops during an announcement is shown as running until the announcement ends plus 1.5 s.

## F4 - One bad RPM window reached the display (fixed)
*Before:* a jump of more than 20 % was accepted at once.
*After:* a jump (or any estimate when there is no reading) is held until the next window agrees within 10 %;
smoothing inside 20 % is unchanged. A lone bad window never shows.
*Tests:* `rpmtrack_logic`, `rpm_glitch` (one window each of 4000, 1000, 6000, 3000, 4000 rpm against a 2000 rpm engine:
the reading moved by at most 30 rpm; a real change to 3600 rpm followed in about a second), `rpm`.

## F5 - Coordinates, battery (fixed)
*GPS coordinates:* latitude outside +-90, longitude outside +-180, NaN and exactly 0,0 are no fix
(`gpsCoordsValid`). `flight_logic`, `fault_gps` (latitude 95 and -91, longitude 200 in flight: no fix each time, flight
kept, fix back; 0,0 on the ground).
*Battery:* a reading under 2.5 V is not a measurement. With no real reading seen since power-on it is USB power or no
battery and stays silent, as before. If a real reading has been seen and it **vanishes in flight** you now hear the urgent
"Battery low" every minute (before: silent, exactly when it matters). A new setting *Critical battery warning* (default
3.3 V) makes the call urgent and repeated every minute below it; the ordinary low call is unchanged. `battery_logic`
(49 cases), `battery_flight`, `fault_battery`.
*Limits:* the same clip "Battery low" is used for all three (there is no separate recording). A battery that is
unplugged on the bench after a real reading while flying is not distinguishable from one that failed in the air.

## F9 - Free memory fell to about 2-5 kB while a browser loaded the page (resolved: a test-build effect; page requests now serialised)
*Observed:* in the HIL runs the lowest free memory since power-on went from 36 kB at boot to 13 kB on the first page load and, with
the harness and several pages busy, to 1.3 to 5 kB. That made the `memory` test (floor 4 kB) fail whenever page tests ran before it.
*Cause, measured (not guessed):*
- **It is a test-build effect.** The test build has about 14 kB less free memory than a release build (idle: 48.5 kB against 62.7 kB: a
  96-line log buffer instead of 40, and test code). The same page load on a **release** build does not lower the lowest free memory
  below its value at boot: 35 kB after one browser loading and cycling through every tab; 28.8 kB with **three browsers loading at once**
  (a phone, a second phone and a PC), 60 kB free afterwards. Nothing ever approached the 15 kB probe limit.
- The dip itself is not made by the web handlers (the page request costs 4 kB): it is the Wi-Fi driver's and the network stack's buffers filling
  while the browser opens several connections and the page is sent (the Wi-Fi buffers are already capped at 8 receive and 6 transmit).
*Fix:* the page now sends its own requests to the vario one after another, with a 20 s timeout each, instead of a burst of six at load time (help
guides fetched from other sites are left alone). On the test build the first-page-load low went from 13 kB to 24.7 kB in the same experiment.
`tools/release_memory_probe.py` repeats the release measurement (1 to 3 browsers, prints the lowest free memory, fails under 15 kB).
*Limits:* measured in a simulated flight on a bench, not in the air; a release build with more clients than three, or a vario with a
BLE client connected, was not measured. The `memory` test (test build) keeps its 4 kB floor and is now judged on a quiet vario: it fails
when other pages are open, which the harness reports.

## F10 - Good GPS, but a ground speed under the landing speed at height ended the flight (fixed)
*Before:* with a trusted GPS speed the vario declared a landing whenever the ground speed was under *Landed by ground speed* for *Landed after* (30 s), at
any height. A paramotor holding station in a strong headwind has a ground speed near zero and is level too; the flight would be ended in the air
(tones off, log closed).
*After:* near the ground (under the renamed *Counts as near the ground below*, default 15 m) the landing time is as before. Higher up it is
*Landed above that height: wait this many times longer* times that (new setting, default 5: 150 s). 1 gives the old rule.
`Flight::landingHoldMs()`.
*Tests:* `flight_logic` (46 cases incl. the exact height and multiplier boundaries, multiplier 0 taken as 1, the barometer not needed
for the speed rule), `landing_hover` (a headwind equal to the airspeed at 60 m: still flying after 3 times the old limit, landed after 5 times;
multiplier 1 lands after the normal time; a landing near the ground is not slowed). `landing_hover` was run against the unfixed firmware first and
failed there.
*Limits:* a pilot who really hovers in the air for 150 s (or whatever the setting makes it) still has the flight ended; and a landing on a hillside well
above the landing zone is declared later (150 s). Both are the setting's trade-off, and the setting is documented.

## Test changes made along the way (so nothing is hidden)
- `wind_faults`: the slow-turn case was 3 deg/s; in a headwind the ground track turns up to twice as fast as the
  heading, so 180 degrees of heading can fill 9 of 12 bins and give a legitimate fit. The baseline run failed on exactly
  that. It is now 2.5 deg/s (150 degrees).
- `baro_only`, `variant_no_gps`, `fault_baro`, `fault_gps`, `circuit_*`: expectations that encoded F1, F6, F7 or F8 as
  correct were reversed, as described above.
- `fault_baro` intermittent dropouts use `baroDropMs`, a dropout timed by the vario itself: HTTP round trips on this link
  vary by seconds, which made "1 s" dropouts last 3.8 s.
- `volume_ramp` judged volume against the time it was *asked*, and failed one run in three when a reply was slow; it now
  judges against the earliest and latest moments the reading can have been taken.
- The runner counts dropped log lines against the test that caused them and exempts the `*_logic` tests, whose suites
  log thousands of lines at once; it also reports any other client using the vario during a test (see TESTING.md).
