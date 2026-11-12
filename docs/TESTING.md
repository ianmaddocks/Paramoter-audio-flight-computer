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
listed in `DROPS_ALLOWED` (`boot`, which restarts the vario, and `memory_events`, which floods it on purpose).
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
  (`baroFault=dead|range|stuck|noise`, `baroNoise`, `baroSpike`) and `windflush=1` (forget the wind samples so far).
  The test build is compiled at the release optimisation level (the debug level made the image too big for the OTA slot).
- **Unit suites on the vario.** There is no host compiler for this project, so software-level tests of production
  functions run on the vario: `GET /api/sim?unit=<rpm|geo|wind|circuit|battery>` runs a suite (src/test/UnitSuites.cpp,
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
| `rpm_logic`, `geo_logic`, `wind_logic`, `circuit_logic`, `battery_logic` | the unit suites above: RPM from synthetic spectra (speeds, pulses per rev, noise, silence, range limits); angles, distance, bearings, clock positions; the wind fit (accuracy, either turn, 0/360, noise, refusals, averaging) and glide over the ground; circuit leg order and turn points, base turn from height, overshoot, wrong way; battery conversion, curve and low-battery call |
| `rpm` | synthetic engine through the real microphone path: stopped, idle, up, down, rapid alternation, noise, dead input, recovery |
| `wind`, `wind_faults` | simulated aircraft in a known wind: head/tail/cross ground speed and drift, takeoff estimate, circling fits (both turns, 0/360, zero wind, changes, convergence); no fit from straight/half/slow turns, refusals, GPS loss, manual wind |
| `circuit_right`, `circuit_left_wind`, `circuit_events` | **the whole landing circuit flown by a scripted pilot** from the first press of the button to the ground: leg order, turn directions, turn points against independent geometry, base turn from height, wind change, wrong way, GPS loss, ignored turn call |
| `fault_baro`, `fault_gps`, `fault_battery` | sensor faults: detected ones (no data, impossible pressure, GPS loss, few satellites) and recovery without reboot; undetected ones recorded (frozen/spiking barometer, weak GPS fix, impossible latitude) as in docs/TEST_FINDINGS.md |
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
| `ui_actions` | mute, test voice and landing-zone buttons |
| `ui_guidance` | flying display, emergency/approach banners, log list and delete |
| `ui_settings` | every setting shown; editing, units, greyed-out boxes, restart marks |
| `ui_spectrum` | spectrum graph, tone marker, pause (skipped on a build made with `-DPV_HAS_SPECTRUM=0`) |
| `ui_help` | Help tab and the Help links on the settings groups: the right guide for the hardware, links that stay in the docs folder and point at sections that exist |
| `ui_system` | tabs, logging viewer, firmware box, version line |
| `ota_guard` | firmware upload refused without the right password |
| `memory` | free memory not run down, and never dangerously low |

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
