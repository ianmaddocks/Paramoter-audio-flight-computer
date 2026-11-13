# Pilot interface update

The aim: a user-friendly instrument built around the pilot. Show only what matters now, say the important things aloud so the
pilot never has to look, and make every control safe to use with a gloved finger in the air.

**Nothing here has been verified on hardware or by listening.** The behaviour is tested in the simulator (see
[TESTING.md](TESTING.md)); the new voice clips are generated but need the filesystem upload described at the end.

## What changed

### Button (physical)

| | Before | Now |
|---|---|---|
| Single press | start / cancel landing approach | **Update me**: height, landing zone, wind, battery. Never starts or cancels anything. |
| Double press | start / cancel emergency glide | start / cancel **landing approach** |
| Long press (0.8 s) | mute | **Hold 1.5 s**: start / cancel **emergency glide** (rising cue at 0.8 s; let go earlier and nothing happens) |
| (none) | | **Triple press**: mute / unmute (now decided 0.6 s after the last click) |
| (none) | | **5 presses, then 5 presses + 2 s hold** (ground only): factory reset, with spoken instruction and cancel |
| Feedback | none | a click on every press |
| Gap between clicks | 0.4 s | 0.6 s (gloves) |
| No GPS | every press mutes | single = tell height and battery; double, triple, hold = mute |

Starting and cancelling guidance are now deliberate gestures, so an accidental touch can only ever tell. This **changes
muscle memory**: try it on the ground before relying on it.

### Audio

- **Update me** (button or page): "Height … Landing zone, 12 o'clock, 500 metres." then "Wind, 3 o'clock, 11 kilometres per hour. Battery, 25 minutes."
  (percent when the minutes are not known). Clock positions only while moving; the landing zone, wind and clock are left out when not known.
- **Battery low:** "Battery low. Approximately 25 minutes remaining. Landing zone 1.2 kilometres. Turn me off until you need help landing." Parts that are not
  known are left out; the advice only when the vario can remember the flight (GPS and a landing zone).
- **Battery critical** and **Battery reading lost** have their own leads (before: all three said "Battery low").
- **Barometer failed** / **Barometer back**, spoken in flight (before: the vario tones just stopped, which sounds like "no lift").
- **Signal tone** before every urgent call.
- **Leg names** with the circuit turns: "Turn right now, downwind".
- **Quiet during guidance:** routine height calls are skipped while an approach or emergency is active (flight time already was).
- A missing voice clip is skipped, and the messages that matter fall back (`battery_critical|battery_low`, `baro_failed|~400`), so a vario whose
  clips are not updated still says "Battery low" and sounds a tone for a barometer failure.

### Flight tab (page)

- **By phase:** one card headed **Actions** at a time. On the ground: **Audio test**, **Mute vario**. In flight:
  **Landing support**, **Emergency support**, **Update me**, **Mute vario**. **Full screen** sits below both. (Buttons renamed: Test voice to Audio test,
  Tell me to Update me, Landing approach to Landing support, Emergency glide to Emergency support, Set LZ here moved to Settings → Landing zone as **Use current position**; LZ = takeoff is the *Landing zone* setting.)
- **Alert strip** under the banner: Battery low / critical (with minutes and distance to the LZ), Barometer failed, No GPS fix. Clears itself.
- **GPS tile:** the signal quality is the big word (strong / good / ok / poor / none), the satellites underneath.
- **Battery tile:** minutes left when known, else the percentage; volts and percent underneath.
- **Background noise** and the **RPM confidence** are tuning data: hidden unless *Show background noise and RPM confidence on the flight tab* is on.
- **Glide ratio and the height graph stay** (the glide ratio is how a pilot calibrates the landing helper).
- **Ground speed tile** has a small **compass** (a ring with N, E, S, W and an arrow inside) for the track against absolute north; the Wind tile shows a windsock (the sock streams the way the wind blows) and the Landing zone tile an arrow, both relative to the track.
- **Quick settings** card first on the Settings tab: volumes, units, circuit direction, glide ratio, trim airspeed. They are the same settings as in their
  groups and follow each other.
- **Logs line** says "456 kB free" (no "of 1024 kB").
- **Reset flight state** button on the Logs & system tab.

### Resume at power-up

A vario switched off in flight (to save the battery, as the low battery message suggests) or reset in the air now carries the flight on when switched on
again, **only on positive evidence that it is still flying**. At the first trusted GPS fix with a time it checks, once:

1. the saved record says flying and has a GPS time and position;
2. it is not older than *Resume the flight if switched back on within* (default 3 hours; 0 = never);
3. the position is where an aircraft could have got to in that time (40 m/s, plus the save interval and 1 km);
4. the aircraft is **moving at flying speed right now** (so a vario switched on at the launch never resumes an old flight).

Then: landing zone, ground reference (heights), wind and flight time (including the time it was off) are restored and it says "Flight resumed".
Anything else drops the record. With no GPS time and fix within 3 minutes it gives up for good. A flight is never taken on trust at power-up, so the
vario cannot start believing it is flying when it is not; and **Reset flight state** (button on the Logs & system tab, or `POST /api/action?do=reset_flight`)
is a manual way out. A resumed flight lands by the normal rules (ground speed under the landing speed for the landing time).

## Settings added

| Setting | Group | Default |
|---|---|---|
| Resume the flight if switched back on within (0 = never) | GPS (Advanced) | 180 min |
| Show background noise and RPM confidence on the flight tab | Engine RPM (Advanced) | off |

| Landed above that height: wait this many times longer | Takeoff & landing | 5 |
| Learn how fast I turn from the first two turns | Landing approach | on |

(Settings from the previous phase: *Counts as near the ground below* (it was called *Landed without GPS speed: only below*), *Barometer frozen after*,
*Critical battery warning*.)

## Not done, and limits

- **Not listened to.** The signal tone, click and cue levels, and the new clips have not been heard on the speaker; the tests check the log and the
  queued clip names, not the sound.
- **Voice clips:** 18 new clips are generated in `data/v` (approximately, remaining, turn_me_off, battery_critical, battery_lost, baro_failed, baro_back, wind, kmh,
  mph, knots, battery, percent, upwind, crosswind, downwind, base, final). They reach the vario only with `pio run -e release -t uploadfs`, which
  **erases the stored flight logs** (download any you want first). Until then the vario works, with the fallbacks above.
- **Resume** needs a GPS; a vario without one does not resume (it has no landing zone to remember). A flight resumed after a long time off relies on
  the GPS time: if the GPS never gives a time, nothing resumes.
- **Switching on below flying speed** (for example hanging in a strong headwind) does not resume; a landing zone saved with Use current position is still there.
- **Same-day flights:** the vario does not resume if you were standing still at the first fix, even if it had been flying.
- Not changed: the physical single-press delay is now 0.6 s (the time the vario waits for a second press).
