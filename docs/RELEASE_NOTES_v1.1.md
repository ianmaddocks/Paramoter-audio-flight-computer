# ParaVario v1.1

Release tag `v1.1` (commit `d3089a2`), built as the `release` PlatformIO environment: 1,412,560 bytes, 94.8 kB under the 1.5 MB update slot.
Changes since `1.0` (42 commits). Simulator and bench results are not verification of flight behaviour; nothing here has been flown except
where the owner's flight logs are mentioned.

## Flying
- **Flight screen:** new tile order and layout (Height / Flight time, Vario with the 10-minute height graph / Ground speed, Landing zone / Wind,
  Glide ratio / Battery, Actions, GPS / Engine RPM); wind shows speed as the main value with the direction below it; flight time reads "Ready" on
  power-up and "Landed" after a flight.
- **Vario sounds only while flying** (a firmware rule, so a stored setting cannot leave it sounding on the ground).
- **Height trace** is recorded only while flying, kept between flights with a gap marker, and not kept across a power cycle.
- **"GPS fixed" is announced only after the fix has held for a configurable time** (default 10 s), not on every flicker.
- **Direction arrows** show or hide with a speed hysteresis decided by the vario (`moveShowMs`, `moveHideMs`, `moveHoldS`).
- **Landing zone:** "Use current position" button replaces "Set LZ manually" / "LZ = takeoff"; takeoff is assumed to be into wind.
- **Flight-safety defects F1-F10** from the earlier phases are fixed (see `docs/TEST_FINDINGS.md`).

## Flight logs
- Logs are written in stretches of about 32 kB (one IGC file to the owner), with RPM, wind, battery, GPS quality, noise and the **applied vario and
  speech volumes** in extension records, so the log can drive the whole Flight screen.
- **Replay** on the page: slider, pause/play, speed, exit, and a chart of the absolute volume through the flight.
- **Making room never stops a flight recording:** earlier days are deleted first, then today's older logs are thinned to one fix in 5, then cut to the
  minimum an IGC file needs (one in 5, then one in 20), then today's oldest logs are deleted, and only then is the start of the current flight trimmed.
- A first flight that was lost to log pruning is fixed. Space shown in the log list: "N kB free: room for about M more minutes".

## Audio
- **All 95 voice clips are one pack file** (`voice.pak`) played through a linear resampler; stored at 11 kHz (410 kB), which gives about 600 kB for logs.

## Wi-Fi, Bluetooth, setup
- **Names:** the Wi-Fi and Bluetooth names are plain settings whose default includes the MAC suffix. Bluetooth manufacturer is "Ian Maddocks", model
  "ParaVario <release>".
- **Maintenance Wi-Fi:** an on/off switch with its own sub-section (assigned address, maintenance SSID and password), a spoken "connected" message, and a
  **retry every 5 minutes on the ground** after giving up if the router was joined earlier; a router joined before a restart is looked for again after it.
- **Factory reset** by five quick presses and a sixth held for 2 s, with spoken prompts.
- **Settings:** Firmware Update moved to Settings / Advanced; Logs & system tab before Settings; a Restart button when a restart is required.
- Current settings became the defaults (except the maintenance network details).

## Reliability fixes (found on the bench on 15 Nov)
- **Crash:** the log-list web request ran the vario out of memory and restarted it. The list now costs about 1.5 kB instead of 14 kB with 32 logs.
- **Watchdog restart:** a client that stopped taking data in the middle of a reply held the main loop until the 30 s watchdog restarted the vario. Replies
  are now sent in packet-sized parts, the watchdog is fed between writes, and a stalled client is dropped after one failed write.
- **Low-memory guard:** web requests are answered 503 "busy" (and retried by the page) when under 12 kB is free or the largest block is under 4 kB.
- **Router link:** while joined, a small packet goes to the router every 10 s and the vario rejoins if the router has not answered for 90 s (on the
  ground only).
- **Diagnostics kept across a crash or restart:** `/api/nettrail` (48 recent network events) and `/api/coredump` (the chip's crash record, decoded with
  `tools/crash_decode.py`); `tools/net_watch.py` prints the trail when the vario returns.
- Page: the Spectrum tab no longer flashes at load; UI events (compass, landing-zone arrow) are written to the log with the values that caused them.

## Tests and tools
- The hardware-in-the-loop suite now has 83 tests (new: factory reset, names, maintenance retry, log segments and room, replay, height, moving, voice
  pack, log-list memory, event trail, stalled client). `tools/release_memory_probe.py` measures the release build.
- Release build result (15 Nov): lowest free memory 29.2 kB with three browsers loading at once (limit 15 kB).

## Known open issues
- On the test build three memory tests still fail (`memory_events`, `soak`, `memory`: lowest free memory 1-4 kB at moments); cause not identified.
- The vario was unreachable on the router (joined by its own count) three times on 15 Nov without a restart; cause not proven, the new router check has not
  been seen to act.
- `fault_baro` fails intermittently; earlier unexplained task-watchdog resets (13 Nov).
- No single clean full run of the suite exists on the final code (see `docs/TEST_FINDINGS.md`, F11).
