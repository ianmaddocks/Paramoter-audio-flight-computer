# ParaVario v1.3

Release tag `v1.3`, built as the `release` PlatformIO environment: about 1.45 MB, about 59 kB under the 1.5 MB update slot.
Changes since `v1.2`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Areas and the Map tab
- **Up to 25 areas** (was 3), 3 to 24 corners each. Areas far from the aircraft are checked less often. A fix for the turn that avoids a restricted area.
- **Uploads are limited to 5 kB** (about 180 corners in all). A 14 kB upload ran the web server out of memory and restarted the vario; the page now checks the size before sending, and the vario answers 400 for anything over the limit.
- **The Map follows you in flight** from takeoff, with an arrow along your track (a dot when still). Dragging the map stops it; opening the Map tab again follows again.
- **Removed:** the *Add corner at cross*, *Add corner at my position* and *Follow me* buttons, and the corner count in the area list. Tap the map to add a corner.

## Settings
- **Advanced settings** still hold Wi-Fi, Wind, Logging and Glide. Battery, Firmware Update, GPS and Engine RPM are on a page of their own, `/advanced`, which nothing links to (type the address: `http://192.168.4.1/advanced`). It has the same tabs as the other pages.
- The settings reply is streamed a field at a time instead of being built whole, which takes pressure off memory.

## Reliability
- **Low-memory restart (last resort):** if free memory stays under 12 kB for 30 s (6 kB in flight), the vario saves the flight state and restarts; the flight is resumed as after any restart in the air. Never in the first five minutes after a start. Settings: *Restart if free memory stays below* and *... for this long* (Logging group; 0 = never). It should never act in normal use.
- Several memory fixes found by the memory tests: the circuit suite's wind estimator on the heap, smaller test-time peaks.

## Version display
- The version is now the git description, for example `v1.3`, or `v1.3-4-gabc1234` between releases (with `-dirty` after it for uncommitted changes). The build time (yyyymmdd-hhmmss) is in the start-up log ("Built ...") and in the status as `stamp`.

## Documents
- New **API guide** (`docs/API_Guide.md`) for software that talks to the vario. The test results folders (`results/ui`, logs) are no longer in git.

## Known issues
- In the test build, `memory_events` still fails by a small margin (its lowest free memory falls about 0.4 kB further than allowed during a settings save that overlaps log polling); the release build's lowest free memory is 25 kB or more. `soak` was not rerun after the latest changes, `speech` needs a quiet room, and `fault_baro` is flaky.
- The new voice clips for the area warnings are not installed (see the v1.2 notes); the warnings use tones and "Turn right now" until then.
