# ParaVario tests

The tests run against a real vario over Wi-Fi. A special firmware build can replace the sensor
inputs with simulated ones, so a whole flight can be played on the bench; a script on the PC
sets up each situation and checks what the firmware did.

## Running

1. Flash the test build (debug build plus the simulation, off until a test switches it on):

   ```
   pio run -e test -t upload
   ```

2. Run the tests (about fifteen minutes; they make noise, so a quiet room helps the microphone
   checks):

   ```
   python tools/hil_test.py
   ```

Useful options:

| Option | Effect |
|---|---|
| `python tools/hil_test.py flight ui_` | only tests whose name contains one of the words |
| `--list` | list the tests |
| `--no-audio` | skip the checks that listen with the microphone |
| `--no-ui` | skip the web page tests |
| `--memory` | print the vario's free memory after each test |
| `--shuffle [SEED]` | run the selected tests in random order (shows they do not depend on each other; the seed is printed) |
| `--soak-minutes N` | length of the `soak` flight (default 10) |
| `--volume 30` | speech and vario volume used during the tests (default 20 %) |
| `-v` | print every log line received |

The suite runs with the setting *GPS module* on "Always present" so the GPS tests do not depend on what the vario has seen; `gps_detect` tests Auto-detect itself.

The script saves the vario's settings first and puts them back afterwards, switches the
simulation off, and deletes the flight logs the tests created. Flash the release build again
before flying.

**Exit codes:** 0 all passed; 1 a test failed; 2 nothing to run (a name matched no test, the error lists the
tests); 3 the tests passed but a cleanup step failed (each cleanup step runs on its own and is reported).
Every test starts from a clean state (simulation off, flight reset, test settings). A test also fails if log
lines were dropped during it (the log feed lost lines, so it may have judged an incomplete log) unless it is
listed in `DROPS_ALLOWED` (`boot`, which restarts the vario, and `memory_events`, which floods it on purpose) or
is a `*_logic` unit test (which runs production code over thousands of simulated seconds in one request and judges the
suite's own report, never the log). Drops are counted against the test that caused them.

**Outside activity.** A phone or PC page left open, or a BLE client (FlyGaggle), changes timing and memory and can drop
log lines. The harness marks every request it makes with an `X-HIL` header (its headless browser too); the vario counts
the web clients that made a request in the last 15 s without it (`wifi.webOthers` in `/api/status`, whichever network
they came in on), the stations on its own access point (`wifi.apClients`) and the BLE connections (`ble`). Before and
after each test the script reads them and prints a WARNING under any test that ran with somebody else connected; the
summary lists those tests, and `--memory` prints the counts on each memory line. Close other pages before a run.
A failure prints the vario's state, the simulator state and the last log lines.

## How it works

- **Simulated inputs** (`src/test/Sim.*`, compiled only with `PV_TEST=1`): barometer altitude,
  GPS (generated NMEA sentences fed through the real parser), battery voltage and background
  noise level, and switches that make the vario behave as if it had no GPS module or no microphone
  (`hasGps=0`, `hasMic=0`), and one that makes it forget that a GPS was ever seen (`gpsForget=1`). They are set with `GET /api/sim?key=value&...`; `reset=1` returns the flight
  logic to its power-on state. In every other build the hooks compile to nothing.
  Added for the larger tests: a **synthetic microphone** (`micMode=synth|dead|real`, `micHz`, `micAmp`,
  `micNoise`: an engine of known firing frequency, noise, or a dead input in place of the ADC samples), a
  **simulated aircraft** (`fly=1`, `hdg`, `air`, `windFrom`, `windSpd`, `turn`, `turnTo`, `turnRate`: ground speed and track
  follow from heading, airspeed and wind, so the true wind and the flown path are known), **barometer faults**
  (`baroFault=dead|range|stuck|noise|nan`, `baroNoise`, `baroSpike` with `baroSpikeN` samples, `baroStep` a persistent
  offset, `baroDropMs` a dropout timed by the vario itself), `rpmGlitch=<rpm>` (one analysis window reports that RPM) and
  `windflush=1` (forget the wind samples so far), `button=press|holdcue|single|double|triple|hold` (inject a decoded button
  event: the decoding of real presses is the `button_logic` unit suite) and `utcBase=<seconds>` (added to the simulated GPS clock, which
  starts at 12:00:00 at power-on and runs with the uptime: a test sets it right after a restart to make a saved flight any age it likes). The simulated barometer always carries about 3 cm of noise, as a real sensor does
  (without it a still simulated sensor would repeat one value for ever and look frozen).
  The voice clips for the pilot-interface messages are not needed by the tests: they judge the log (the clip names), not the sound.
  The test build is compiled at the release optimisation level (the debug level made the image too big for the OTA slot).
- **Unit suites on the vario.** There is no host compiler for this project, so software-level tests of production
  functions run on the vario: `GET /api/sim?unit=<rpm|geo|wind|circuit|battery|flight|baro|rpmtrack|button|status|alerts|resume|audio>` runs a suite (src/test/UnitSuites.cpp,
  RpmTest.cpp) on inputs of its own and returns `{"pass":n,"fail":m,"failures":[...]}`; `tonetest=1` is the vario tone
  decision. The HIL tests `*_logic` call them. The unit tests prove the maths; the HIL tests prove the whole system
  behaves: the two are meant to complement each other, e.g. `circuit_logic` drives the circuit state machine with
  scripted positions, `circuit_right` flies the real thing end to end.
- **Checks from the log.** The script reads the debug log feed (`/api/debuglog`) continuously
  and looks for the lines a behaviour produces: `SAY` (every phrase spoken, by clip name), `TONE`
  (vario tone changes), `RDY` (ready banner), `FLT`/`APP` (takeoff, landing), `IGC`, `BAT`,
  `VOL`, `NAV` (the once-a-second state trace), `MEM` (memory low-water mark).
- **Sound.** Where it matters that the speaker really produced the sound, the microphone
  spectrum (`/api/spectrum`) is checked for the tone or for speech.
- **Web page.** The `ui_*` tests open the vario's real page in a headless Edge or Chrome,
  driven over the DevTools protocol by the script itself (nothing to install), and check what
  the page shows and what its buttons and inputs do.

## What is covered

| Test | Checks |
|---|---|
| `boot` | start-up beep, pause, "Ready" (timed by the vario itself and read from its status reply) |
| `settings` | settings API round trip and limits |
| `vario_climb`, `vario_sink` | tone pitch in the log, and heard by the microphone |
| `vario_thresholds` | climb and sink tones off just under their thresholds, on just over them at the calculated pitch, and off again (log, and the microphone unless `--no-audio`); speeds are well either side of each threshold |
| `button_logic`, `status_logic`, `alerts_logic`, `resume_logic`, `audio_logic` | unit suites for the pilot interface: button decoding (click counts, 0.6 s gap, triple at once, hold cue at 0.8 s and hold at 1.5 s, abandoned hold, bounce); the spoken "update me" (order, clock position, units, what is left out); the battery messages (minutes, landing zone distance, advice to switch off, critical and lost leads) and the barometer failed / back calls; when a saved flight is resumed (age, reach, speed, missing time or position); the signal tone before urgent calls only |
| `button_actions` | every gesture injected: click and hold cues, single = update me and never starts or cancels guidance, double = approach on/off, hold = emergency on/off, triple = mute, and the no-GPS mapping |
| `alerts_flight` | "Barometer failed" / "Barometer back" spoken in flight; routine height calls silent while an approach guides |
| `resume_flight` | **restarts the vario in simulated flight** (about 8 minutes): resumed after 30 min at flying speed (same LZ, heights, flight time, then lands normally); not resumed when too old, standing still, in an impossible place, switched off by setting, or with no GPS time within 3 minutes; a pilot reset ends a flight and nothing resumes after it |
| `ui_layout` | **what a look at the screen would show**, on a 360 px wide phone (the user's) and a 320 px one: no text cut off ("39 k..."), nothing drawn over anything else, nothing off the edge, in the ground view, flying, at 120 km/h, in mph and miles and in knots and metres, with a landing zone 55 km away, with three alerts at once, and on the Settings and Logs tabs. Saves a screenshot of each to `results/ui/` (the harness's browser emulates a 360 px, 2x phone for every page test) |
| `landing_hover` | F10: hovering in a headwind at height with a good GPS is not a landing as quickly as on the ground (multiplier 5), multiplier 1 is the old rule, a landing near the ground is not slowed |
| `ui_pilot` | the flight tab by phase (setup card on the ground only), the alert strip (low and critical battery, barometer, no GPS, each clearing), GPS and battery tile wording, the noise tile only on request, Quick settings first and in step with their groups, Update me and Reset flight state buttons |
| `rpm_logic`, `geo_logic`, `wind_logic`, `circuit_logic`, `battery_logic`, `flight_logic`, `baro_logic`, `rpmtrack_logic` | the unit suites above: RPM from synthetic spectra (speeds, pulses per rev, noise, silence, range limits); angles, distance, bearings, clock positions; the wind fit (accuracy, either turn, 0/360, noise, refusals, averaging, stale samples before a wind change, convergence after one) and glide over the ground; circuit leg order and turn points, base turn from height, overshoot, wrong way, and when "steer" may be called (not during a called turn; called for a missed, stalled or wrong-way turn); battery conversion, curve, low, critical and lost-reading calls; the landing decision (trusted speed, weak or lost GPS, height and vertical-speed limits) and GPS coordinate validity; the barometer plausibility checks (impossible and NaN pressure, lone outliers, steps, rate limit, frozen reading, recovery); the RPM reading logic (jump confirmation, hold through speech and dropouts) |
| `rpm` | synthetic engine through the real microphone path: stopped, idle, up, down, rapid alternation, noise, dead input, recovery |
| `rpm_speech`, `rpm_glitch` | RPM held through three 6 s announcements and a real change straight after; single bad windows (4000, 1000, 6000, 3000, 4000 rpm) never reach the reading while a real change is followed |
| `wind_change` | wind changes in flight: old samples must not bias the first fit, the second fit in the new wind gives it, the third stays |
| `wind`, `wind_faults` | simulated aircraft in a known wind: head/tail/cross ground speed and drift, takeoff estimate, circling fits (both turns, 0/360, zero wind, changes, convergence); no fit from straight/half/slow turns, refusals, GPS loss, manual wind |
| `circuit_right`, `circuit_left_wind`, `circuit_events` | **the whole landing circuit flown by a scripted pilot** from the first press of the button to the ground: leg order, turn directions, turn points against independent geometry, base turn from height, wind change, wrong way, GPS loss, ignored turn call |
| `fault_baro`, `fault_gps`, `fault_battery` | sensor faults and recovery without reboot: barometer no data, impossible and NaN pressure, frozen reading, lone wild samples (held back), persistent steps (accepted once), intermittent dropouts, noise; GPS loss, too few satellites, a weak fix (flight kept), impossible coordinates and 0,0 (no fix); battery flicker and impossible readings |
| `landing_gps_quality` | **a missing, weak or stale GPS must not end a flight**: no GPS, weak fix, too few satellites, no fix, level and at plus or minus 0.2 m/s, at 60 and 200 m for several times the landing time, a 70 s loss, recovery; real landings still found on the barometer alone and with a weak fix |
| `battery_flight` | low (ordinary) and critical (urgent) battery calls in flight, a reading lost in flight (urgent), recovery, and 0 V on the ground (silent: USB) |
| `memory_events`, `soak` | repeated events (settings, log reads, unit suites, resets, tones, mic swaps) with free memory, lowest memory and task stacks judged from the idle baseline; a long simulated flight with memory trend, GPS dropouts and IGC logging |
| `spectrum_setting` | the Advanced setting *Show the Spectrum tab* (default off) removes and restores the tab |
| `vario_logic` | software-level unit test of the tone decision, run on the vario: exact `>=` / `<=` at the thresholds, pitch and beep-rate formulas and limits, every silencing condition (no sensors or sound involved) |
| `mute` | mute silences the tone and says so |
| `auto_volume` | volume against background noise |
| `gps_states` | GPS absent / no fix / weak fix ignored / good fix |
| `flight` | takeoff, height calls, "No GPS", landing, contents of the IGC file |
| `baro_only` | no GPS at all: slow rise ignored, takeoff in a gentle climb, true height, landing |
| `guidance` | approach and emergency: refusals, start-up calls, "on course", cancel |
| `battery` | percentage curve, minutes left, low-battery call |
| `battery_learner` | the discharge learner, on two simulated discharges (in RAM) |
| `speech` | test phrase: clip sequence, and heard by the microphone |
| `volume_ramp` | a volume change is applied gradually at the set rate, never as a jump |
| `glide` | glide ratio from speed and sink: 1 s and long average, none when level or climbing |
| `gps_detect` | the vario finds out for itself whether a GPS is fitted (unknown, then not fitted after 10 s, found when one starts sending, kept when silent), by hand both ways, and the page and its settings list follow |
| `variant_no_gps` | vario with no GPS: ready, takeoff, height, landing, no GPS calls or log, page and settings trimmed |
| `variant_no_mic` | vario without a microphone: set volume used, battery still read, page and settings trimmed |
| `ui_flight` | banner and every tile on the flight tab |
| `ui_actions` | on the ground: Mute vario, Audio test and the two landing-zone buttons (Set LZ manually, LZ = takeoff) |
| `ui_guidance` | flying display, emergency/approach banners, log list and delete |
| `ui_settings` | every setting shown; editing, units, greyed-out boxes, restart marks |
| `ui_spectrum` | spectrum graph, tone marker, pause (skipped on a build made with `-DPV_HAS_SPECTRUM=0`) |
| `ui_help` | Help tab and the Help links on the settings groups: the right guide for the hardware, links that stay in the docs folder and point at sections that exist |
| `ui_system` | tabs, logging viewer, firmware box, version line |
| `ota_guard` | firmware upload refused without the right password |
| `memory` | free memory not run down, and never dangerously low |

**Simulator limits.** None of this verifies hardware behaviour. In particular the barometer checks are exercised on a
simulated sensor; whether a real DPS310 can ever repeat an identical reading for the 5 s of *Barometer frozen after* is not
tested here (the setting can switch the check off), and GPS quality, speech and the engine microphone are likewise simulated.

Not covered: Bluetooth (needs a phone), the Wi-Fi give-up timer, a complete firmware upload (every flash of a
new build exercises it), the push button, and the Restart / Factory defaults buttons. Also not exercised:
the DPS310 re-initialisation path after a real sensor loss (the simulated sensor reproduces its observable
effect only), real microphone acoustics (the synthetic engine replaces the ADC samples), and the audio queue
depth (only task stacks and heap are watched). Suspected defects found while writing the tests are in
docs/TEST_FINDINGS.md.

## Adding a test

A test is a function `t_name(v, a)` in `tools/hil_test.py`, listed in `TESTS`. `v` is the
vario: `reset(v, ...)` sets a simulated situation, `v.sim(...)` changes it, `v.expect(tag,
pattern, since)` waits for a log line, `v.nav()` reads the state trace, `v.page()` gives the web
page. It returns a one-line note or raises `Fail("what went wrong")`.
