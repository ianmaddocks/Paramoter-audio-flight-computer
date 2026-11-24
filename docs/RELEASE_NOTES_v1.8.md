# ParaVario v1.8

Release tag `v1.8`, built as the `release` PlatformIO environment: about 1.45 MB, about 53 kB under the 1.5 MB update slot.
Changes since `v1.3`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Settings
- **Advanced settings** now hold, in alphabetical order: Areas, Emergency, Glide, Landing approach, Landing zone, Logging, Takeoff & landing, Vario tuning, Wi-Fi and Wind. The Settings tab keeps the groups most pilots use.
- **New group, Vario tuning:** Base tone, Tone rise per m/s, Responsiveness, Settle time at power-on and Barometer frozen after moved out of Vario into Advanced settings.
- **Area warnings follow the tick on the Map tab.** The settings *Warn when I leave a safe area* and *Warn about restricted areas* are removed: an area that is ticked on warns in flight, an unticked one does not. Check your areas before flying: they were previously silent unless those switches were on.
- **Quick settings removed** from the Settings tab; every setting is in its group.
- Defaults updated to the current settings (volumes 15 and 10, landing speed 1.4 m/s, on-course tolerance 15°, 20 s of track kept before takeoff).

## Logs & system
- **Check for updates** button: the phone asks GitHub for the latest release (needs mobile data) and shows the installed and latest versions with a link to the release notes. It only sees versions published as GitHub releases.
- The flight log list shows minutes (per flight, and room left) instead of kilobytes.
- "BLE clients" now reads "Bluetooth clients".

## Documents
- The guides and the Feature Reference follow the settings moves above. The guides now say mobile data is needed to open help pages and view the map.

## Known issues
- Not all tests were run on this version. In the last full run (before the settings moves): `soak` failed (lowest free memory fell from 2504 to 1652 B), `ui_guidance` timed out fetching the height history, `memory_events` failed by a margin, and part 3 stopped early after losing contact with the vario; `speech` needs a quiet room. The test cases for the area change and for Check for updates have not been run.
- The new voice clips for the area warnings are not installed (see the v1.2 notes); the warnings use tones and "Turn right now" until then.
