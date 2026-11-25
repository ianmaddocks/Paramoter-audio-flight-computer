# ParaVario v1.19

Release tag `v1.19`, built as the `release` PlatformIO environment: about 1.46 MB, about 50 kB under the 1.5 MB update slot.
Changes since `v1.17`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Updating
- **Install and Cancel.** Once an install starts, the Install button goes away and Close becomes *Cancel* while the update is downloading (it stops the download). Nothing is shown to press while the image is sent to the vario.
- **Battery.** Install is greyed out, with a note in the box, when the vario runs on a battery under 30%. It does not apply on USB power.

## Known issues
- The full test run has not been completed on this version. Earlier failures stand: `soak`, `ui_guidance`, `memory_events`; `speech` needs a quiet room. The test cases for the area change, Check for updates and Install update have not been run.
- An update still needs a usable Wi-Fi link: below about -85 dBm it can fail even with the retries.
