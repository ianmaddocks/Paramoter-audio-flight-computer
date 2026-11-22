# ParaVario v1.2

Release tag `v1.2`, built as the `release` PlatformIO environment: about 1.45 MB, about 62 kB under the 1.5 MB update slot.
Changes since `v1.1` (21 commits). Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Areas (new, GPS module)
- **Areas tab** on the web page: draw up to 3 flat polygon areas (3 to 24 corners each) on a map. Tap the map to add a corner, or drag the map until the
  cross is on a corner and tap **Add corner at cross**; **Add corner at my position**, **Show my position** and **Paste corners** (latitude, longitude
  lines or geojson.io coordinates) also work. The corners are listed under the map in a scrollable window (5 rows) with move up, move down and delete;
  numbered corners can be dragged. Areas can be switched on or off, edited, copied and deleted. The map zooms to all the areas when the tab opens.
- **Safe** areas (green) are places to stay inside, **Restricted** areas (red) places to keep out of. Each area has a **Warn me** distance: safe 0 m
  (default) tells you only when you leave; restricted 500 m (default) warns before you enter. Both can be changed per area.
- **Spoken:** "You have left the safe area" (then "Outside the safe area" repeated, "Back in the safe area" on return, no directions); "Safe area edge, N
  metres" when a safe area has a warning distance; "Restricted area, 400 metres. Turn right to 2 o'clock." when the track would enter a restricted area
  within its warning distance (the smallest turn that keeps out); "You are in a restricted area. Turn left to 9 o'clock." inside one. The landing
  approach and emergency glide come first; the areas stay quiet during them.
- **Settings:** a new Areas group: warn when I leave a safe area, repeat that warning, warn about restricted areas, repeat a restricted-area warning.
- **Storage:** the areas are a file (`/areas.json`) on the vario. The page needs mobile data (or another Wi-Fi) to load the map when you draw; flying needs
  no internet. Areas are flat (no heights) and an aid only, not an airspace warning system.
- The new warnings use tones and "Turn right now" until the new voice clips are installed (see known issues).

## Flying
- **Landing approach in a strong wind:** the legs into and across the wind are shortened in proportion to the ground speed (never below 35 %), the
  crosswind speed loss from crabbing is counted, and the downwind leg is not shortened. Default legs are 120 m, circuit height 500 ft, final turn at 50 ft.
- **Status** (was "Update me"): height first, a short pause, then the landing zone and the wind. The single press no longer speaks the battery.
- **Volume tile** with + and -; **Mute until flight** setting; the page buttons show the presses that do the same ([.], [..], [...], [-]).
- **A landing with no GPS speed** is never declared in under 60 s.
- **Height-call lists** (boundaries) can be typed with commas and spaces on an iPhone.
- **Speech low-cut:** the voice is high-passed at 300 Hz (the beeps and the low sink alarm are not filtered), for the small 20 mm speaker.
- **Defaults** are now the owner's settings (except the maintenance Wi-Fi and the manual wind). A vario that already holds stored settings keeps them.

## Documents
- New short **pilot guide** (`docs/User_Guide_(pilot).md`): how to use it, nothing about why. The advanced guide, the feature reference and the README describe
  the Areas tab and the new calls.

## Tests and tools
- The geometry of the areas has a PC test (`test/host/safearea_geo_test.cpp`).
- `tools/flight_review.py` reads an IGC log; `tools/crash_decode.py` and `tools/net_watch.py` from v1.1 are unchanged.
- The unit and hardware-in-the-loop suites were updated for the status, circuit and button changes but were **not run** on this code.

## Known open issues
- The new voice clips (`left_area`, `safe_edge`, `restricted_area`, `in_restricted`, `turn_right`, `turn_left`) are in `tools/vocab.txt` but not in the
  shipped `data/voice.pak`. Until the pack is rebuilt and installed (which erases the flight logs and the stored areas), the area warnings use tones and
  the "Turn right now" clips.
- The on-device test image has no room left (32 bytes spare), so there are no on-device tests for the area logic yet. The plan is two test builds, each
  with half the suites.
- The three memory tests on the test build still fail (`memory_events`, `soak`, `memory`), and there is no clean full run of the suite on this code.
- The vario has sometimes been unreachable on the router without a restart; cause not proven (`/api/nettrail` and `/api/coredump` record what they can).
