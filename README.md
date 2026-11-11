# ParaVario

**An audio vario and flight computer for paramotor flying.**

ParaVario is open-source firmware and hardware for a small, battery-powered instrument that tells you what
the air and the aircraft are doing through sound, so you can fly with your eyes up. Beeps rise in pitch as
lift gets stronger, a low tone warns of heavy sink, and a voice calls your height and flight time. The
Advanced version also talks you around a landing circuit, and guides you back to the field if the engine
stops. It runs on a Seeed XIAO ESP32C3 and is built with PlatformIO.

> **A home-built instrument, not certified aviation equipment.** It can be wrong, lose its GPS, or run
> out of battery. Never rely on it as your only source of height, position or landing guidance.

## What it does

**Every version (Basic and Advanced)**

- **Vario beeps** that rise in pitch and quicken with the climb rate, and a **sink alarm** tone.
- **Voice calls** of your height as you pass heights you choose, and of your flight time.
- **Automatic takeoff and landing detection**: "Take off" and "Landed, flight time ...".
- **Bluetooth** to FlyGaggle and other apps (`$LK8EX1` and `$LXWP0` vario sentences).
- A **web page** on the vario's own Wi-Fi network, opened on your phone: live height, a 10-minute height
  graph, vario, flight time and battery tiles; every setting; firmware update; a built-in Help tab.
- A smooth volume control that never jumps, and a battery gauge that learns how long your battery lasts.

**Advanced version only (adds a GPS receiver and a microphone)**

- With the **GPS**: ground speed, **glide ratio**, wind estimate, **landing zone**, a spoken **landing
  approach circuit**, **emergency glide** guidance back to the landing zone, and **IGC flight logs**
  (with the 30 seconds before takeoff included).
- With the **microphone**: the speaker volume **rises with the noise around you** (smoothly), **engine
  RPM** read from the engine sound, a background-noise tile and a live sound-spectrum page.

## Two versions

| | Basic | Advanced |
|---|---|---|
| Hardware | barometer and speaker | adds a u-blox M10 GPS and an analogue microphone |
| Build | `pio run -e basic` | `pio run -e release` |
| Guide | [User_Guide_(basic).md](docs/User_Guide_(basic).md) | [User_Guide_(advanced).md](docs/User_Guide_(advanced).md) |

Each is a separate build, and the version shown on the web page ends in `-basic` or `-advanced` so you
can see which program a vario is running. The two share one code base (see
[Hardware variants and build flags](#hardware-variants-and-build-flags)).

## Hardware

- **Seeed XIAO ESP32C3** (the brain, with Wi-Fi and Bluetooth)
- **DPS310** barometer (I2C)
- **MAX98357A** amplifier and a small speaker (I2S)
- Battery with a voltage divider on an ADC pin, and a **push button**
- *Advanced only:* a **u-blox M10 GPS** module (UART) and an **analogue microphone** module (ADC)

The pin assignments are in [src/Pins.h](src/Pins.h).

## Getting started

1. Install [VS Code](https://code.visualstudio.com/) with the PlatformIO extension, and clone this repository.
2. **Set your own update password**: change `custom_ota_password` in [platformio.ini](platformio.ini). It is
   asked for whenever new firmware is sent to the vario over Wi-Fi, so do not leave the shared one.
3. **First install, over the USB cable.** Advanced: `pio run -e release_usb -t upload`. Basic:
   `pio run -e basic_usb -t upload`. Then install the voice clips (they are in `data/`):
   `pio run -e release_usb -t uploadfs` (or `-e basic_usb`).
4. **Join the vario's Wi-Fi** (named `ParaVario-` followed by four characters unique to your unit; the
   default password is `paramotor`) and the page opens on your phone, or go to `http://192.168.4.1`.
5. **Later updates go over Wi-Fi**: `pio run -e release -t upload` (or `-e basic`), from a PC joined to
   the vario's network, or on your home network once you have set the *Server network* in settings.

To change what the voice says, see `tools/vocab.txt` and `tools/make_voice.py`. The full set of build,
update and maintenance steps is in the user guides.

## Documentation

| Document | What it is |
|---|---|
| [User_Guide_(basic).md](docs/User_Guide_(basic).md) | How to fly and use the Basic vario (no GPS, no microphone) |
| [User_Guide_(advanced).md](docs/User_Guide_(advanced).md) | How to fly and use the Advanced vario (GPS and microphone) |
| [Feature_Reference.md](docs/Feature_Reference.md) | Every tile, button, sound and setting explained, Basic first, then Advanced |
| [TESTING.md](docs/TESTING.md) | The hardware-in-the-loop test suite that checks the firmware on a real vario |
| *Architecture* (below) | How the firmware is put together, for anyone changing it |

The vario's web page also has a **Help** tab, and a Help link on each group of settings, that open these
guides on GitHub.

## Testing

The firmware is checked by running a whole simulated flight on a real vario. A special build replaces
the sensors with simulated ones, and a script on the PC sets up each situation, reads the vario's log,
listens to the speaker through the microphone, and drives the web page in a headless browser. See
[TESTING.md](docs/TESTING.md).

---

## Architecture

How the firmware is put together, for anyone changing it.

### Hardware variants and build flags

Seeed XIAO ESP32C3 with a DPS310 barometer (I2C), a MAX98357A speaker amplifier (I2S), a battery
divider on an ADC pin, a push button, and optionally a u-blox M10 GPS (UART) and an analogue
microphone (ADC). Pin numbers are in `src/Pins.h`.

The GPS and the microphone are optional. Which are fitted is a build choice (`src/Hardware.h`):

| Build (`pio run -e ...`) | GPS | Microphone | Use |
|---|---|---|---|
| `release`, `debug` | yes | yes | full vario |
| `basic`, `basic_usb` | no | no | simplest variant: barometer and speaker only (`basic_usb` is for the first install over the USB cable) |
| `test` | yes | yes | debug build plus simulated sensors, for `tools/hil_test.py` |

`-DPV_HAS_GPS=0` and `-DPV_HAS_MIC=0` can also be used one at a time in a build of your own.
`-DPV_HAS_SPECTRUM=0` leaves out just the microphone spectrum feature (the Spectrum tab with its page and
script, `/api/spectrum` and the code that produces it); noise level, automatic volume and engine RPM are
not affected. It is implied by `PV_HAS_MIC=0`, so the Basic builds contain none of the spectrum code or
page text.

The version string carries the variant: `20261111-131219-advanced` (both parts), `...-basic` (neither),
`...-basic+gps` or `...-basic+mic` (one). It is built in `src/Version.h` from the same flags and appears in
the boot log, the page footer and the status reply, so a glance shows which program a vario runs.

What changes without the part:

| | Without GPS | Without microphone |
|---|---|---|
| Vario tone, height calls | unchanged | unchanged |
| Height | above the takeoff point (barometer only) | unchanged |
| Takeoff / landing | from height gain and a steady vario | unchanged |
| Ready to fly | barometer working and settled | unchanged |
| Speed, wind, landing zone, approach and emergency guidance, IGC log | not available | unchanged |
| Bluetooth | vario sentences only (`$LK8EX1`, `$LXWP0`) | unchanged |
| Volume | unchanged | the set volume (no rise with noise) |
| Engine RPM, spectrum page | unchanged | not available |
| Battery reading | unchanged | ordinary ADC reads instead of the microphone's sampler |
| Push button | every press mutes / unmutes | unchanged |
| Web page, settings | GPS tiles, buttons and settings hidden | noise/RPM tiles, Spectrum tab and settings hidden |

The code asks `hw::hasGps()` / `hw::hasMic()` instead of using `#if`, so there is one code path.
In normal builds these are compile-time constants (unused branches are removed) and the drivers
`sensors/Gps.cpp` and `sensors/Mic.cpp` are left out entirely. The test build can switch a fitted
part "off" at run time, which is how the reduced variants are tested on a full vario.

### Layout

```
src/
  main.cpp            Arduino setup()/loop(), nothing else
  App.h               the App struct: all sensors, flight logic and outputs
  Hardware.h          optional hardware (GPS, microphone)
  Settings.*          every tunable, with its web-page description; stored in NVS as JSON
  Log.*               log ring buffer (serial + the web page's Logging section)
  Pins.h, Version.h
  app/
    App.cpp           the main loop: order and timing of everything
    VarioSound.*      power-on beep + "Ready", climb/sink tone
    Alerts.*          GPS fix / lost, low battery
    Trace.*           periodic debug-log lines (debug/test builds)
  sensors/
    Baro.*            DPS310 + Kalman filter: altitude and vario
    Gps.*             u-blox driver; GpsData.h is the plain snapshot everything else reads
    Mic.*             ADC sampler task: noise level, engine RPM, spectrum (+ battery samples)
    Battery.*         voltage, percentage, minutes left; BatteryLearn.* learns the discharge
  flight/
    Flight.*          altitude fusion, ground reference, takeoff / landing -> NavState
    Wind.*            wind from the takeoff run and from circling
    Glide.*           glide ratio (ground speed over sink rate) for the Glide ratio tile
    HeightHistory.*   last 10 minutes of height, for the graph on the Flight tab
    Announcer.*       height-boundary, periodic height and flight-time calls
    Approach.*        landing circuit guidance
    Emergency.*       glide-to-landing-zone guidance
    FlightStore.*     flight state kept across a restart
  nav/                NavState (the shared snapshot) and geometry helpers
  audio/
    Audio.*           I2S output task: speech clips + vario tone mixer
    Speech.*          builds phrases (numbers, heights, clock directions) from clips
    Volume.*          volume control: noise compensation and rate limiting
  comms/
    BleFeed.*         Bluetooth telemetry (Nordic UART service)
    WebUi.*           Wi-Fi + HTTP API; WebPage.h is the page (Flight, Settings, Spectrum,
                      Logs & system and Help tabs); WifiMemory.cpp caps Wi-Fi buffers
  logging/IgcLogger.* IGC flight log on LittleFS
  input/Button.*      push button
  test/Sim.*          simulated sensors (test build only)
```

### Data flow

```
 Baro ─┐
 Gps ──┼─> Flight::update() ──> NavState ──┬─> Wind, Approach, Emergency, Announcer ──> speech
 (GpsData)                                 ├─> VarioSound ──> tone
                                           ├─> IgcLogger, BleFeed
 Mic ──> noise ──> Volume ──> audio gains  └─> WebUi (status page)
```

`NavState` (`nav/Nav.h`) is rebuilt by `Flight::update()` every 100 ms and is the only thing the
rest of the flight logic reads. `GpsData` plays the same role for the receiver: the driver fills
it, nothing else touches the parser, and without a GPS it simply stays "no link, no fix".

### Tasks and timing

| Task | Priority | Does |
|---|---|---|
| audio (`Audio.cpp`) | 5 | fills the I2S buffer: decodes speech clips, generates the tone, applies volume |
| mic (`Mic.cpp`) | 3 | reads the ADC stream, FFT every 0.25 s |
| loop (`App.cpp`) | 1 | everything else |

Main loop, each pass about 1 ms: sensors, button, Bluetooth, web server. Every 20 ms: volume and
tone. Every 100 ms: flight logic, announcements, log, alerts.

Because the sound is produced in its own task, the tone keeps its rhythm while the main loop is
busy (serving the web page, writing the log). What the main loop being busy does delay is a
*change* of tone and the Bluetooth sentences, so anything slow there (a long web request) should
be kept short or split up.

Sound latency: about 30 ms of barometer sampling and filtering, up to 20 ms to the next tone
update, then 64 ms of I2S buffering. The buffer is deliberately that long: erasing flash for the
flight log can stop every task for tens of milliseconds, and a shorter buffer would click.

### Audio behaviour

- Speech has priority over the tone. When speech starts the tone fades out over 5 ms first, and
  fades back in afterwards; nothing is ever cut mid-wave.
- Priorities for speech (`audio::Priority`): Low is dropped if anything is queued, Normal queues,
  High jumps the queue, Urgent also cuts off what is playing.
- Volume (`Volume.cpp`): wanted volume = the setting, raised towards 100 % between the two noise
  thresholds when auto-volume is on. The applied volume moves towards it no faster than the
  "Fastest volume change" setting (default 40 %/s), and the audio task spreads each step over
  100 ms sample by sample. So neither a sudden noise nor a changed setting produces a jump or a
  click. The microphone ignores the vario's own tone and skips blocks that overlap speech, so
  the vario does not hear itself and turn itself up.

### Robustness

- **Loop watchdog** (30 s): if the main loop stops, the vario restarts instead of staying silent.
- **Memory**: about 50 kB is free at run time. Wi-Fi packet buffers are capped
  (`WifiMemory.cpp`), big web replies are sent in single-packet pieces through one fixed buffer,
  and requests wait briefly when free memory is short. `MEM` lines in the debug log show every new
  low and what was running.
- **Uploads**: TCP keepalive on firmware uploads, so a sender that vanishes is noticed in seconds.
- **Bluetooth**: advertising is restarted after every disconnect, and while there is room for a
  second client; a check every 5 s restarts it if it ever stopped. Connection parameters asked
  for (30-60 ms interval, 4 s timeout) tolerate the radio time shared with Wi-Fi.
- **GPS**: fixes are screened for jumps; weak fixes do not provide speed or altitude; altitude far
  from the barometer datum is rejected. A lost receiver is re-detected.
- **Barometer-only flight**: the ground level is held while climbing or sinking, so takeoff is
  detected and height is right without any GPS.

### Settings

Every tunable is a field of `Settings` (`Settings.h`) with one line in the table in
`Settings.cpp` giving its group, label, unit and limits. The web page builds itself from that
table (`/api/schema`), so adding a setting needs no page change. Settings for hardware that is
not fitted are left out of the page (`available()` in `Settings.cpp`).

### Adding things

- **A new optional sensor**: add a `PV_HAS_x` flag and `hw::hasX()` to `Hardware.h`, give the
  driver a stub for when it is absent (see `Gps.h`, `Mic.h`), expose its values as plain data,
  list its settings in `available()`, add `hw.x` to the status reply and a `needx` class to the
  page elements that depend on it.
- **A new announcement**: add the clip to `tools/vocab.txt`, regenerate the voice files, and
  call `speech::say()` from the module that knows when (see `Alerts.cpp`).
- **A test**: see `docs/TESTING.md`.

### Help links

The page's Help tab and the "Help" links on the settings groups show the guides in `docs/` inside the
page, as plain content without the GitHub project around it. The page fetches the Markdown from GitHub's raw
file server, always the latest version, and a small converter in `WebPage.h` renders it, so the phone needs
internet (and the guides must stay within the converter's Markdown subset: headings, lists, tables, code
blocks, quotes, links, bold, italics and `<a id>` anchors). The address is
`PV_HELP_RAW_URL` and `PV_HELP_WEB_URL` in `src/Version.h` (override them with build flags for a fork). Each settings group is
mapped to its section in `docs/Feature_Reference.md` by `GROUP_HELP` in `WebPage.h`; the section
anchors in that document are `<a id="...">` tags, and the test `ui_help` checks that every link
points at an anchor that exists, so renaming a section there needs the map updated too.
