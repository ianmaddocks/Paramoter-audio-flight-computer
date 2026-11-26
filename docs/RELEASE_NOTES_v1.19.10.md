# ParaVario v1.19.10

Release tag `v1.19.10`, built as the `release` PlatformIO environment: about 1.46 MB, about 45 kB under the 1.5 MB update slot.
Changes since `v1.19`. Version numbers now have three parts: small edits and fixes add 0.0.1. Simulator, bench and browser results are not verification of flight behaviour; nothing new here has been flown.

## Barometer
- **Temperature fault fix.** Some DPS310 sensors read about 60 °C because of a fault in the chip, which also skews the pressure. The vario now applies the manufacturer's fix whenever the chip needs it. On the test unit the temperature went from 60 °C to 31 °C, and the pressure from 1022.5 to 1018.2 hPa. Check your height against a known elevation after updating.

## Battery
- **Learning a flat run.** A run that ends with the battery flat (3.35 V or lower) is now learned when the vario next starts, even if it starts on charge while the voltage is still low. Before, such a run was thrown away.

## Updating
- **Progress bar.** The download takes the first 10% and sending the update to the vario the rest, as the sending takes most of the time.
- **Battery.** Under 30% battery, Install is hidden and the box says why.
- **Version check.** It compares all three parts of the version number.

## Settings and pages
- **Appearance.** New setting under Units: Auto (follows the phone), Light or Dark. Default: Auto.
- **Export and Import settings** are now buttons in the System section of Logs & system.
- **Areas map.** The warning distance is drawn as a dotted line around each area: outside a restricted area, inside a safe one. It also works for large distances and jagged outlines.

## Known issues
- The full test run has not been completed on this version. Earlier failures stand: `soak`, `ui_guidance`, `memory_events`; `speech` needs a quiet room. The new and changed test cases (battery learning, area lines, update box, System section) have not been run.
- An update still needs a usable Wi-Fi link: a weak signal can make it fail even with the retries.
