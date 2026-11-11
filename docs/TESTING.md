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
| `--volume 30` | speech and vario volume used during the tests (default 20 %) |
| `-v` | print every log line received |

The script saves the vario's settings first and puts them back afterwards, switches the
simulation off, and deletes the flight logs the tests created. Flash the release build again
before flying.

## How it works

- **Simulated inputs** (`src/test/Sim.*`, compiled only with `PV_TEST=1`): barometer altitude,
  GPS (generated NMEA sentences fed through the real parser), battery voltage and background
  noise level, and switches that make the vario behave as if built without a GPS or without a
  microphone (`hasGps=0`, `hasMic=0`). They are set with `GET /api/sim?key=value&...`; `reset=1` returns the flight
  logic to its power-on state. In every other build the hooks compile to nothing.
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
| `boot` | start-up beep, pause, "Ready" |
| `settings` | settings API round trip and limits |
| `vario_climb`, `vario_sink` | tone pitch in the log, and heard by the microphone |
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
| `variant_no_gps` | vario without a GPS: ready, takeoff, height, landing, no GPS calls or log, page and settings trimmed |
| `variant_no_mic` | vario without a microphone: set volume used, battery still read, page and settings trimmed |
| `ui_flight` | banner and every tile on the flight tab |
| `ui_actions` | mute, test voice and landing-zone buttons |
| `ui_guidance` | flying display, emergency/approach banners, log list and delete |
| `ui_settings` | every setting shown; editing, units, greyed-out boxes, restart marks |
| `ui_spectrum` | spectrum graph, tone marker, pause (skipped on a build made with `-DPV_HAS_SPECTRUM=0`) |
| `ui_help` | Help tab and the Help links on the settings groups: the right guide for the hardware, links that stay in the docs folder and point at sections that exist |
| `ui_system` | tabs, debug log viewer, firmware box, version line |
| `ota_guard` | firmware upload refused without the right password |
| `memory` | free memory not run down, and never dangerously low |

Not covered: Bluetooth (needs a phone), the Wi-Fi give-up timer, a complete firmware upload (every flash of a
new build exercises it), the push button, engine RPM, wind from circling, the approach circuit
beyond its start, and the Restart / Factory defaults buttons.

## Adding a test

A test is a function `t_name(v, a)` in `tools/hil_test.py`, listed in `TESTS`. `v` is the
vario: `reset(v, ...)` sets a simulated situation, `v.sim(...)` changes it, `v.expect(tag,
pattern, since)` waits for a log line, `v.nav()` reads the state trace, `v.page()` gives the web
page. It returns a one-line note or raises `Fail("what went wrong")`.
