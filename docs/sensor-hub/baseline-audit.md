# Sensor hub - Phase 0: baseline audit and proposed architecture

Branch `feature/external-sensors`, cut from release v1.1 (commit `d3089a2`). 15 Nov 2026.
Status: **for review. No firmware has been changed, nothing flashed, nothing committed.**

How to read the figures: **[M]** = measured on the vario or from a build artefact, **[R]** = taken from a test log of this
project (with the file), **[E]** = estimate, **[N]** = not measured yet (what it needs is stated). Nothing from source reading
alone is given as a headroom figure.

## 1. What the vario is today

A paramotor flight computer on a Seeed XIAO ESP32-C3 (single-core RISC-V, 160 MHz, 4 MB flash) [M: `platformio.ini`].
Sensors: DPS310 barometer (I2C), GPS (UART), analog microphone (ADC, noise and engine RPM), battery divider (ADC), one button.
Outputs: I2S amplifier (vario tones and 95 spoken clips), a Wi-Fi access point with a web page and JSON API, BLE (NUS) to FlyGaggle
(GPGGA/GPRMC/LK8EX1/LXWP0), IGC-style flight logs in LittleFS.
Source: 12,410 lines in `src/`. Six pieces of firmware run: the Arduino `loopTask` (everything except audio and microphone sampling,
in one cooperative loop in `appLoop()`), an `audio` task (I2S, 4 kB stack, priority 5), a `mic` task (ADC DMA and FFT, 4 kB,
priority 3), the Wi-Fi/lwIP system tasks, and the NimBLE host and controller tasks.
The web server is the Arduino synchronous `WebServer`: **one request is served at a time, inside the main loop**, every request is a
new connection.

Network today: Wi-Fi in AP mode (SSID and BLE name carry the MAC suffix, e.g. `ParaVario-12B8`), optionally also joined to a
"maintenance" router as a station (AP+STA). The AP is created with the library defaults: channel 1 (follows the router's channel
when joined), **maximum 4 stations** [M: `WiFi.softAP(ssid, pass)` in `WebUi.cpp`; library default in `WiFiAP.h`].
Wi-Fi packet buffers are capped at 8 receive / 6 transmit (`WifiMemory.cpp`). Authentication: WPA2 password on the AP if set
(8+ characters); OTA needs a password that is a published default in the public repo (not a secret); the JSON API itself has no
authentication.

Existing HTTP routes [M: `WebUi.cpp`]: `/`, `/api/status`, `/api/settings` (GET, POST), `/api/schema`, `/api/action`, `/api/logs`,
`/api/log` (GET, DELETE), `/api/debuglog`, `/api/spectrum`, `/api/height`, `/api/coredump`, `/api/nettrail`, `/api/ota`, and in
the test build `/api/sim`.

## 2. Resources

### 2.1 Flash [M]
| Item | Size | Share |
|---|---|---|
| Flash (module) | 4 MB | |
| App slots app0, app1 (OTA) | 1,507,328 B each (0x170000) | |
| Release image v1.1 | 1,412,560 B | 93.7 % of a slot, **94.8 kB free** |
| Test image (same code + test suites) | 1,504,208 B | 99.8 %, 3.1 kB free |
| LittleFS ("spiffs") | 1 MB | voice pack 410 kB, logs/rest about 600 kB free |
| NVS 20 kB, otadata 8 kB, coredump 64 kB | | |

Image sections, release [M: `riscv32-esp-elf-size`]: flash code 1,019,996 B, flash rodata 237,424 B, IRAM code 72,764 B,
data 16,908 B (the test image: 1,080,326 / 261,872 / 73,074 / 17,156).
**There is no room in the partition table for a bigger app without losing OTA or the log filesystem**; the slots are what OTA
needs. Adding about 95 kB to the release is the ceiling before the slot is full, and the **test build, which carries the test
suites, has 3 kB left**: sensor test code cannot be added to it as it stands (see section 6).

### 2.2 RAM [M unless marked]
| Item | Release v1.1 | Test build |
|---|---|---|
| Static (`.data` + `.bss`) | 87.6 kB (16.9 + 70.7) | 103.6 kB |
| Free heap just after power-on (idle) | 56.9 kB, largest block 36.9 kB | about 40 kB [R] |
| Lowest free heap after power-on, idle | 38.9 kB | |
| Lowest free heap, 3 browsers loading at once | **29.2 kB** (`release_memory_probe.py`, pass limit 15 kB) | 1.1-4 kB at moments [R] |
| Heap after loading, idle | 56 kB | 35-39 kB |

Total on-chip SRAM of the ESP32-C3 is 400 kB (a part of which is cache and ROM-reserved); static plus IRAM (160 kB) plus the Wi-Fi,
BLE and lwIP runtime leaves the heap figures above.

- **Minimum heap during representative flight operation on the release build: [N].** The 10-minute simulated flight (`soak`) can
  only run on the test build (it needs the simulator). Test-build figures: free heap stable at 34-42 kB between rounds, lowest
  1.1-8 kB [R: `results/full_run9_normal.log`, `memory_rerun.log`, `run20_rerun.log`]. The test build has about 15-20 kB
  less free than the release (96-line log buffer, test code), so the release is better by that much, but how much better under
  flight load has not been measured. Measuring it needs a release build with the tracing counters (section 7).
- **Stack high-water marks [R, test build only]:** `loopTask` **1,480 B free** of 8,192 (the lowest seen), `audio` 2,120 B free of
  4,096, `mic` 2,008 B free of 4,096 [`results/full_run14.log`, `memory_events`]. The main task has 18 % of its stack in reserve.
  Code that runs in the main loop (JSON parsing of requests) must not add to the worst-case depth without raising
  `CONFIG_ARDUINO_LOOP_STACK_SIZE` (costs heap one for one) or running in its own task.
- **Task execution times: [N].** The loop is not instrumented. What is known: the loop is cooperative with a `delay(1)`; the
  task watchdog is 30 s; a single stalled web write can hold the loop up to about 16 s (found and bounded on 15 Nov, see
  `TEST_FINDINGS.md` F11). Audio and the microphone have their own tasks and are not held by the loop; GPS parsing, the
  barometer, the flight logic and BLE notifications **are** (they run in the loop).

### 2.3 Wi-Fi and API cost
- Wi-Fi: 8 rx / 6 tx dynamic buffers (about 13 kB + 10 kB worst case), up to 16 lwIP sockets, **4 AP stations**. A phone page
  polls `/api/status` roughly once a second and loads the schema and settings at start. Today's failures were all in this
  area [R, `TEST_FINDINGS.md` F9, F11]: memory exhaustion in a log-list request (crash), a stalled client holding the loop
  (watchdog restart), "joined to the router but unreachable" three times after free memory fell to 1-2 kB (cause not proven).
- The web server turns requests away with 503 when less than 12 kB is free or the largest block is under 4 kB, and holds new
  requests when under 20 kB is free (`kMinHeapToServe`).
- Release memory probe with three browsers: lowest free 29.2 kB, no requests refused [M, 15 Nov].

### 2.4 Pins [M: `Pins.h`]
D0 mic/ADC, D1 battery ADC, D2 button, D4-D6 I2S, D7-D8 I2C (DPS310), D9-D10 GPS UART. **D3 is the only free pin and it cannot
be used as an analog input with Wi-Fi on.** A wired sensor is therefore not possible without sharing a bus (I2C has room; the
GPS UART does not). The proposal here is wireless sensors, so this is noted, not a blocker.

### 2.5 OTA limitations
Two 1.5 MB slots; a failed or interrupted upload leaves the old image running (good); uploads fail repeatedly when a phone or
page is connected (WinError 10054, seen constantly on 15 Nov: the 8-buffer receive cap and the single-threaded server);
an upload that fails part-way can leave the vario unreachable for up to an hour or more (seen 3 times on 15 Nov, the old
code comment says the same). A sensor hub that keeps sensors connected to the AP during an upload makes this worse.
The test build leaves 3 kB in its slot.

## 3. Test baseline

Located: `tools/hil_test.py` (harness, 83 tests by name today, not 47: 47 was the count some time ago; the suite grew with
every change), on-device unit suites (`/api/sim?unit=<name>`, test build), `tools/release_memory_probe.py`, `docs/TESTING.md`.
**Current actual status** (final code of 15 Nov, test build, vario on the bench; simulator results are not hardware
verification of flight behaviour):
- No single clean full run on the final code exists. Full run of the final code (`results/full_run14.log`): 69 passed,
  13 failed, **11 of those because the vario went unreachable in the middle of the run** (cause: the stalled-client watchdog restart,
  fixed afterwards). Re-run of the affected tests afterwards (`results/run20_rerun.log`): 14 passed, 3 failed.
- **Still failing: `memory_events`, `soak` and `memory`** - all free memory on the test build (lowest 972 B; drift 166 B/min against a
  150 limit; lowest since power-on 3 kB). Not understood yet. `wind_change` failed in the full run and passed in the re-run.
  `fault_baro` is intermittent (failed several times earlier in the day with "two wild samples in a row", passed in run 14).
- New on 15 Nov and passing: `log_list_memory`, `net_trail`, `stalled_client`, `maint_retry`, `igc_segments`, `igc_room`,
  `ui_replay`, and others.
- The test build image (1,504,208 B) leaves 3 kB: **new HIL tests that need new firmware code in the test image do not fit.** The
  sensor simulator must live on the PC (a separate process acting as the sensor over Wi-Fi), which is also what the brief
  asks for; any on-device test code needs the release-side code to shrink first (section 6).
- The release build is exercised by the memory probe only; no flight-logic test runs on it.

Known open issues at the baseline (from `TEST_FINDINGS.md`): three memory tests failing as above; unexplained "joined but
unreachable" episodes; earlier unexplained task-watchdog resets (13 Nov); free memory dips to about 1 kB on the test build
under several clients; the `MyVario-Wifi` test leaves the vario broadcasting a test network name if a run dies.

## 4. Largest resource consumers [M / R]

Flash (release image 1.41 MB): the web page (embedded HTML/JS, `WebPage.h`), the Wi-Fi + BLE (NimBLE) + lwIP stacks from the
Arduino core, ArduinoJson, the 95-clip voice pack is **not** in the app image (it is in LittleFS, 410 kB).
RAM: Wi-Fi + lwIP + BLE buffers (largest, not controllable beyond the buffer caps already applied), the audio output ring and
speech/clip buffers, the microphone FFT buffers, the debug log ring (40 lines in the release, 96 in the test build), the web
server's per-request `String` and JSON buffers, the settings table (101 settings), `HeightHistory`.
Not measured per module (a per-component memory map of the release image is a Phase 0 item still open: it needs
`pio run -t size` with map output and a heap-caps snapshot, no flashing).
Possible savings if capacity is needed [E, unmeasured]: the spectrum tab and its buffers (off by default), the debug log ring,
the web page size (minify), `HeightHistory` (kSamples), the second OTA slot cannot be shrunk.

## 5. Proposed architecture (for review)

**Scope (owner, 15 Nov 2026): this project defines the API in both directions and implements the vario's side.** The vario's endpoints are called by
sensors (pair, register, heartbeat, live telemetry); the sensor-side endpoints (status, history, acknowledge, config) are **defined here, implemented by
each sensor project, and called by the vario**. Nothing about how a sensor is built, stored or updated is specified or assumed beyond them.
Generic channel model (device -> channels -> measurements; type, unit, range); EGT and CHT are sensor
types, not special cases.

**Choices I recommend, and why they differ from the brief's suggested endpoints where they do:**

1. **Live telemetry: small batched pushes, not one HTTP request per sample.** With the existing synchronous one-request-at-a-time server, many
   single-sample requests would each cost a connection in the main loop, on a server that already had a reliability failure under 2-3 clients on
   15 Nov. The client pushes **one POST every 2 s or so with up to 16 records (with sequence numbers)**. HTTP is the only transport in version 1;
   the owner has not decided on anything else.
2. **History is pulled by the vario, from a dedicated low-priority task**, never from the main loop (the brief's hybrid model): a blocking HTTP client
   call in `appLoop()` is exactly what the 30 s watchdog and the audio/GPS timing forbid. One bounded batch at a time (32 records, under 2 kB), acknowledged
   only after the batch is written to persistent storage. This is the largest new risk on the vario: it adds the vario's first HTTP *client*, which the
   15 Nov stalled-write watchdog restart shows must have short timeouts and a fed watchdog. Needs a measured stack and heap cost (estimate: 6-8 kB stack,
   4-6 kB transient heap [E]).
3. **Registry: fixed arrays**, `kMaxPaired` entries persisted in NVS or a small LittleFS file (first implementation 2, ceiling 8; 4 and 10
   only after load testing), one fixed-size state record per channel (value, unit code, status flags, sequence, last-seen),
   no heap allocation per sample. Telemetry queue: a fixed ring of 16 batch records.
4. **Pairing and authentication (the owner's proposal):** a keyed hash, with a random pairing secret that both sides know and that is shared out of
   band (shown on the vario's page, typed into the client, never sent). Every request carries an HMAC-SHA256 (8-byte tag) over method, path, session id,
   counter and body, keyed with that secret: integrity and origin, no encryption, no TLS. Because the secret is the key itself, it is 64 bits of
   randomness (16 hex characters) rather than a short code, so recorded traffic cannot be brute-forced offline. Wrong-vario detection: the pair and
   register replies carry a MAC only a vario holding the secret can make. Replay: a new random session id for each register plus a rising counter. The limits
   are stated in `protocol/README.md` section 7.
5. **Time:** `GET /api/v1/system/time` (UTC, validity, source `gps`, uncertainty), clients keep uptime plus boot id and
   sequence; records are tagged "untimed" and the vario associates UTC from the (uptime, session) -> UTC offsets it
   observes during contact; no UTC is invented without GPS time.
6. **UI:** optional tiles (EGT, CHT per channel) added to the existing engine area; the status JSON gets a new `sensors` object
   (additive, so existing clients do not break); a stale value is shown greyed with its age, never as current.
7. **AP capacity:** raise the AP's maximum stations from 4 to a measured number (6-8 depends on heap), because **four paired
   sensors plus the pilot's phone would already exceed today's limit** and a sensor must not push the phone off the vario.

The API document, OpenAPI and sequence diagrams are Phase 1 (`protocol/`).

### Offloading (section 9 of the brief)
Under the owner's scope, anything sensor-specific (calibration, filtering, storage, diagnostics) is the sensor's own business and outside this
project. Nothing in the vario is both non-critical and large enough to be worth moving elsewhere; GPS time authority, flight state, vario/altitude and
landing guidance stay. Conclusion: no offloading.

### Processor (section 10 of the brief)
The question is whether the ESP32-C3 has headroom. **The evidence says the headroom problem is not CPU but memory and the Wi-Fi/web
path:**
- RAM: 56.9 kB free idle and 29.2 kB at the lowest with three browsers on the release; the test build already runs at 1-4 kB at
  moments and has had failures that follow low memory.
- The new load is mostly Wi-Fi: more stations, more connections, more buffers - on the part of the system that was found
  fragile today.
- Flash: 94.8 kB left in the release slot; the sensor framework (endpoints, registry, persistence of acked positions and records, UI) is estimated at 30-60 kB [E];
  fits, with little margin; the test build does not.
- CPU: the loop time is not measured, so no CPU-bound claim can be made either way.

| Option | Assessment [E unless stated] |
|---|---|
| 1. Keep the C3 | Possible for 1-4 sensors **if** the telemetry transport is batched and the AP limit is raised within a measured heap budget. Margin is small. |
| 2. Reduce existing features | Frees perhaps 5-15 kB heap and 20-40 kB flash (spectrum buffers, debug ring, page size); needs your approval per item. |
| 3. Offload to an external processor | Nothing worth offloading (see above). |
| 4. Move to ESP32-S3 | XIAO ESP32-S3 has the same footprint and the same D0-D10 labels (to be verified against the pinout), 512 kB SRAM, optional 8 MB PSRAM, two cores, 8 MB flash on the Sense/Plus variants; a second core would take Wi-Fi/web off the audio/GPS loop. Costs: new board bring-up, different I2S/ADC details, re-testing everything, a different power draw. |

**Recommendation: keep the ESP32-C3 for Phases 1-2 and decide at the Phase 2 gate on measured numbers** (release heap
low-water with a simulated 4-sensor load, AP at 6-8 stations, plus the loop-time instrument). If the measured lowest free
heap with four simulated sensors and a phone falls under 15 kB (the existing probe's limit), recommend the XIAO ESP32-S3 as
the smallest upgrade. I am not recommending an upgrade now: nothing measured shows the C3 cannot do it, and the
existing reliability problems are better fixed first.

## 6. Constraints and risks

| # | Risk | Evidence | Mitigation |
|---|---|---|---|
| R1 | Wi-Fi/web path is fragile under load | 15 Nov: crash, watchdog restart, three unreachable episodes | Fix and measure first; batched telemetry (one request per ~2 s), not per-sample requests |
| R2 | 4-station AP limit | `WiFi.softAP` defaults | Raise to a measured value; budget heap per station [N] |
| R3 | Main loop stack reserve 1.5 kB | `memory_events` | Sensor work in its own task, bounded JSON, no parsing in the loop's deepest path |
| R4 | Test image has 3 kB free | build | PC-side simulator; trim the test image before any on-device sensor test |
| R5 | Release image has 94.8 kB free | build | Budget 70 kB; flash savings list ready |
| R6 | Memory tests fail on the test build | run20 | Understand before adding load; do not hide behind the test build's smaller heap |
| R7 | Several clients on the vario's AP at once (phone + sensors) | AP limit 4, free heap | Raise the limit only to a measured value; first target 1 device + 1 browser |
| R8 | Unmeasured loop timing | none today | Add a loop-time and task-time counter in a measuring build (needs flashing approval) |
| R9 | Clients on the vario's AP share its channel with the maintenance router | AP+STA uses one radio and one channel | Document: a client must tolerate a channel change when the router is joined |
| R10 | History pull (the vario as HTTP client) can block or monopolise Wi-Fi | the 15 Nov stalled-client watchdog restart | Dedicated task, short timeouts, watchdog fed, one bounded batch per interval, paused when memory is short or flying with little free heap |

## 7. What is not yet measured (needs your approval to do)

1. **Loop and task execution times, release-build heap low-water during flight load, per-module memory map.** Needs a build
   with counters flashed to the vario (a firmware change, but diagnostic only) - the brief says not to flash hardware without
   approval.
2. A fresh full run of the existing suite on the final code (about 2.5 hours, uses the vario exclusively, test build).
3. AP stations vs heap (needs a firmware change: the station limit) - Phase 2 work, so not now.

## 8. Phased plan, complexity and risk [E]

| Phase | Content | Effort | Main risk |
|---|---|---|---|
| 0 | This audit; plus items in section 7 once approved | small | - |
| 1 | API specification (both directions), OpenAPI, examples, diagrams, test vectors | small-medium | review |
| 2 | Registry, identity, version handling, time and register endpoints, simulated sensor (PC-side) | medium | heap, AP limit, loop stack |
| 3 | Pairing and security | medium | usability, wrong-Vario rejection |
| 4 | Live telemetry, validation, stale/fault handling, status JSON | medium | web path load |
| 5 | History pull task: sensor-side calls, validate, persist, acked, duplicates, gaps, restarts | large | blocking client calls, durability across restarts, bounded RAM |
| 6 | UI tiles and diagnostics | medium | page size, JSON growth |
| 7 | Load, soak (60 min or more), 4 and (if supported) 10 sensors | large | the existing memory failures |
| 8 | Documentation and release | medium | - |

Test IDs `HUB-001` to `HUB-035` as in the brief; the PC-side simulator would plug into `tools/hil_test.py` and its seed controls.
Estimated sizes are my estimates, not measurements.

## 9. Decisions (owner's answers, 15 Nov 2026)

1. **No flashing of the vario for this feature set yet.** Everything for the sensor hub is built and checked without putting
   firmware on the hardware: compiled with the existing PlatformIO environments, protocol checked on the PC. This also means the
   measurements in section 7 are **postponed**, not cancelled.
2. **Fix the three failing memory tests on `main` first:** agreed in principle. They can only be investigated on the hardware, which
   decision 1 rules out for now; nothing was changed on `main`. Whether they can be fixed at all is not known (see section 3: cause
   not identified).
3. **First target: one paired sensor and one browser.** The vario capacity table in the protocol (README section 12) is set to that;
   four paired sensors and ten are not claimed.
4. **Live-telemetry transport:** undecided by the owner; version 1 specifies HTTP only.
5. **Commits:** nothing is committed on this branch until the owner says so; Phase 0 and Phase 1 files are uncommitted.

## 10. Phase 1 status
**Scope clarified by the owner on 15 Nov:** the API is defined in both directions. The vario's endpoints are called by sensors; the sensor-side
endpoints (status, history, acknowledge, config) are defined here and called by the vario, and each sensor project implements them. The draft was restored
to that shape (history is pulled by the vario and acknowledged). The sensor feasibility paper (ESP8266 limits) stays removed: how a sensor is built is not
this project's concern.
The authentication is the owner's proposal: a random 64-bit pairing secret shared out of band, used directly as the key of an HMAC on every request in
both directions.
The API draft (`docs/sensor-hub/protocol/`: README, OpenAPI checked valid against the OpenAPI 3.0 schema with all example messages checked against it,
examples, diagrams, test-vector reference code) awaits the owner's review. Nothing has been implemented, flashed or committed.
