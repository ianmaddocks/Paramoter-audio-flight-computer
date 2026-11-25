# ParaVario v1.13

Release tag `v1.13`, built as the `release` PlatformIO environment: about 1.45 MB, about 54 kB under the 1.5 MB update slot.
Changes since `v1.8`. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Updating
- **Install update.** When Check for updates finds a newer release, its box has an *Install update* button: the phone downloads that version from this repository (mobile data), sends it to the vario and the vario restarts. It is refused while flying. If something goes wrong the old version keeps running.
- **No firmware password.** Updates from the computer (`pio run -e release_ota -t upload`) and from the phone need no password. The firmware box is gone from the hidden page.

## Logs & system
- **Backup and restore** (Export settings, Import settings) is now a card on this tab instead of the hidden page. The file holds every setting and the areas.

## Fixes
- **A firmware upload under 100 kB is refused** ("image too small to be firmware"). An upload of a few bytes starting with the firmware marker made the update library write zeros over the whole flash (bootloader, settings, file system) and the vario would not start again. Real firmware images were never affected.

## Known issues
- The full test run has not been completed on this version. Earlier failures stand: `soak`, `ui_guidance`, `memory_events`; `speech` needs a quiet room. The test cases for the area change, Check for updates and Install update have not been run.
- The new voice clips for the area warnings are not installed (see the v1.2 notes).
