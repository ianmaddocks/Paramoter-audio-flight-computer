# ParaVario v1.15

Release tag `v1.15`, built as the `release` PlatformIO environment: about 1.45 MB, about 54 kB under the 1.5 MB update slot.
Changes since `v1.13`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Updating
- **Install update responds at once.** Tapping it turns the button into *Installing…* and the box says *Starting…*, so it is not tapped twice.
- **The page stops polling the vario while an update is sent.** The vario's web server takes one request at a time; the page's own status requests queued up against it and could make an update fail part-way.
- **A weak Wi-Fi link no longer cuts an update off as easily.** A dead link is noticed after 24 s instead of 11 s.

## Known issues
- The full test run has not been completed on this version. Earlier failures stand: `soak`, `ui_guidance`, `memory_events`; `speech` needs a quiet room. The test cases for the area change, Check for updates and Install update have not been run.
- A vario with a weak Wi-Fi signal (about -85 dBm or worse) can still fail an update; move it closer to the router and try again.
