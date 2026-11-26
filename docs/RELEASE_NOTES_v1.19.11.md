# ParaVario v1.19.11

Release tag `v1.19.11`, built as the `release` PlatformIO environment: about 1.46 MB, about 45 kB under the 1.5 MB update slot.
Changes since `v1.19.10`. Nothing new here has been flown.

## Updating
- **Downloaded.** The box says *Downloaded 100%* once the download is complete.
- **Clearer retries.** When the connection to the vario drops while the update is sent, the box now says it will try again by itself and to keep the page open, then *Trying again (attempt 2 of 3)*.

## Known issues
- Sending an update to the vario can stall and drop part-way even close to it; the page then sends it again by itself. The cause is not yet known.
- The full test run has not been completed on this version (see v1.19.10).
