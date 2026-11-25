# ParaVario v1.17

Release tag `v1.17`, built as the `release` PlatformIO environment: about 1.46 MB, about 52 kB under the 1.5 MB update slot.
Changes since `v1.15`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Updating
- **An update survives stalls of a weak Wi-Fi link.** The vario's main-loop watchdog allows 120 s instead of 30 s while an update is received (the vario restarted when the link stalled for 30 s), and a link silent for 80 s is cut cleanly with the old version kept.
- **The page sends again after a lost connection.** It waits for the vario to answer, then sends the update again, twice at most. If the vario came back already on the new version, the update is counted as done.
- **The update box closes by itself.** After an install the page waits until the vario answers again and then reloads, so the box and the page show the new version.
- **The latest release can always be installed.** The button reads *Install update*, *Reinstall vX.Y* or *Install vX.Y (older)*.

## Known issues
- The full test run has not been completed on this version. Earlier failures stand: `soak`, `ui_guidance`, `memory_events`; `speech` needs a quiet room. The test cases for the area change, Check for updates and Install update have not been run.
- An update still needs a usable Wi-Fi link: below about -85 dBm it can fail even with the retries.
