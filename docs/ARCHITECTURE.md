# ParaVario architecture

How the firmware is put together, for anyone changing it. The user-facing description is in
`docs/User_Guide_(basic).md` and `docs/User_Guide_(advanced).md`, with every tile, button and
setting explained in `docs/Feature_Reference.md`; how to test is in `docs/TESTING.md`.

## Hardware and variants

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

## Layout

```
src/
  main.cpp            Arduino setup()/loop(), nothing else
  App.h               the App struct: all sensors, flight logic and outputs
  Hardware.h          optional hardware (GPS, microphone)
  Settings.*          every tunable, with its web-page description; stored in NVS as JSON
  Log.*               log ring buffer (serial + web page debug log)
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
    WebUi.*           Wi-Fi + HTTP API; WebPage.h is the page; WifiMemory.cpp caps Wi-Fi buffers
  logging/IgcLogger.* IGC flight log on LittleFS
  input/Button.*      push button
  test/Sim.*          simulated sensors (test build only)
```

## Data flow

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

## Tasks and timing

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

## Audio behaviour

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

## Robustness

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

## Settings

Every tunable is a field of `Settings` (`Settings.h`) with one line in the table in
`Settings.cpp` giving its group, label, unit and limits. The web page builds itself from that
table (`/api/schema`), so adding a setting needs no page change. Settings for hardware that is
not fitted are left out of the page (`available()` in `Settings.cpp`).

## Adding things

- **A new optional sensor**: add a `PV_HAS_x` flag and `hw::hasX()` to `Hardware.h`, give the
  driver a stub for when it is absent (see `Gps.h`, `Mic.h`), expose its values as plain data,
  list its settings in `available()`, add `hw.x` to the status reply and a `needx` class to the
  page elements that depend on it.
- **A new announcement**: add the clip to `tools/vocab.txt`, regenerate the voice files, and
  call `speech::say()` from the module that knows when (see `Alerts.cpp`).
- **A test**: see `docs/TESTING.md`.
