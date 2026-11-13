# ParaVario user guide - Advanced vario (with a GPS module)

ParaVario is an audio vario and flight computer for paramotor flying. It beeps when you climb, calls out your height above the landing zone (LZ), talks you around a landing circuit, and guides you back to the LZ if the engine stops. It sends vario and GPS data to FlyGaggle over Bluetooth and has a settings page you open on your phone.

This guide is for the **Advanced** vario, which has a GPS module. A vario without one (the **Basic** vario) has its own guide, [User_Guide_(basic).md](User_Guide_(basic).md). Every tile, button and setting is explained in [Feature_Reference.md](Feature_Reference.md): Part 1 for what both versions have, Part 2 for what the GPS and microphone add. This guide shows how to use the vario; the reference explains what each control does.

There is only one program for both. The vario finds out at power-on whether a GPS module is fitted and shows the Advanced or the Basic features to match.

## The push button
| Press | What it does |
|---|---|
| **Single press** | **Update me**: speaks your height, the landing zone (clock direction and distance), the wind and the battery. It never starts or cancels anything, so an accidental touch does no harm. |
| **Double press** | Starts **landing approach** guidance ("Landing approach…"). Double press again to cancel ("Approach cancelled"). Does nothing while emergency mode is on. |
| **Hold** (1.5 s) | Starts **emergency glide** guidance ("Emergency, glide range…"). Hold again to cancel ("Emergency cancelled"). Starting emergency mode cancels any landing approach. |
| **Triple press** | Mutes or unmutes the **vario beeps** ("Vario muted" / "Vario on"). Spoken calls are never muted. |

You hear a short **click** on every press, and a rising tone 0.8 s into a hold that says "keep holding". Let go before 1.5 s and the hold is abandoned: nothing happens. Release between the clicks of a double or triple press takes up to 0.6 s (wide enough for gloves). A single press is spoken about 0.6 s after you let go, because the vario waits to see whether a second press follows.

**This changed:** earlier versions started the approach with a single press, the emergency with a double press and muted with a long press. Starting guidance is now the deliberate gesture and a single press only tells. On a vario without a GPS a single press tells the height and battery, and a double press, triple press or hold mutes.


## Before you fly

1. Switch on. You hear a short climb beep at the vario volume (so you can judge the level), then, a second later, **"Ready"**.
2. Wait outside for **"GPS fix"**.
3. Optionally, open the web page on your phone (see [The web page](#the-web-page)). The banner at the top turns green and reads **Ready to fly** once the GPS has enough satellites, the barometer works and the landing zone is known. It shows the satellite count against the minimum, for example `7 / 6 satellites`. The banner is only a guide; whether to fly is your call.
4. Take off. Takeoff is detected automatically and you hear **"Take off"**. The spot where you stood before the takeoff run becomes the landing zone, and its height becomes zero for all height calls. To use a different LZ, see [Landing zone](#landing-zone).

## If the GPS is missing or stops

A vario with no GPS module (see [User_Guide_(basic).md](User_Guide_(basic).md)) simply leaves out what depends on it: no speed, wind, landing zone, approach or emergency guidance and no flight log, and a single press of the button only tells the height and the battery, while a double press, triple press or hold mutes or unmutes the vario.

How the vario knows: at power-on it listens for the GPS module. If it hears nothing for a few seconds it treats the GPS as not fitted. Once a GPS has been seen it is remembered, so a GPS that later goes quiet shows `GPS not responding` rather than disappearing. You can also set it by hand: **GPS module** in Settings, Advanced settings, GPS (Auto-detect, Always present or Not fitted).

A GPS that stops working in flight leaves you with the vario, heights and height calls; "No GPS" is spoken after 5 seconds.

## Switching off in flight to save the battery

When the battery runs low you hear, for example, "Battery low. Approximately 25 minutes remaining. Landing zone 1.2 kilometres. Turn me off until you need help landing." You can switch the vario off to save what is left, and switch it on again when you start your landing approach or want the direction to the landing zone. **Switch it on a minute or two before you need it**: it has to find the GPS first.

What the vario remembers when it is switched on again in flight:

- It remembers the flight if it was switched off within *Resume the flight if switched back on within* (GPS settings, default 3 hours) **and** it is moving at flying speed at the first good GPS fix, in a place the flight could have got to. Then you hear "Flight resumed" and it still knows the landing zone, the heights, the wind and the flight time, including the time it was off.
- It does **not** resume if the record is too old, the position is impossible, it is standing still (for example switched on at the launch), or no GPS position and time arrive within 3 minutes. It then starts as on any other day, so it can never come up believing it is flying when it is not.
- If you switch it on again below flying speed (for example hanging in a strong headwind), it does not resume; a landing zone you set with **Set LZ manually** is still remembered.
- If it ever does think it is flying when it is not, press **Reset flight state** on the Logs & system tab.

## What you will hear

**Vario**
- Rising beeps when climbing faster than the climb threshold (default +0.3 m/s). Higher pitch and faster beeps mean stronger lift.
- A continuous low tone when sinking faster than the sink alarm threshold (default −2.0 m/s).
- Beeps pause while speech is playing.

**Automatic calls**
| Call | When |
|---|---|
| "Take off" | Takeoff detected. |
| "250 feet" … | You climb or descend through a height boundary. |
| "400 feet" | Every 60 s while flying (current height); skipped during approach or emergency guidance. |
| "Flight time 1 hour 5 minutes" | Every 5 minutes; skipped during approach or emergency guidance. |
| "Landed, flight time …" | Landing detected. |
| "No GPS" | GPS lost for 5 s in flight. |
| "Barometer failed" / "Barometer back" | The barometer stops working in flight (after 3 s; repeated every minute) / reads again. The vario beeps are off meanwhile. Urgent. |
| "Battery low. Approximately 25 minutes remaining. Landing zone 1.2 kilometres. Turn me off until you need help landing." | Battery below the warning voltage; repeats every 5 minutes. The minutes appear when the vario has learned them, the distance when there is a fix and a landing zone. |
| "Battery critical…" | Below the critical voltage: urgent, every minute, same details. |
| "Battery reading lost" | A real battery reading disappears in flight. Urgent, every minute. |
| (rising signal tone) | Before every urgent call, so you recognise it over engine noise. |

All heights are above the landing zone, not above the ground you're currently flying over.

## Landing approach (double press)

The circuit is built around the LZ. Upwind and final are flown into wind along the same line, and upwind passes over the LZ:

```
                 wind blows from this side
                            |
                            v
      +<----- crosswind -------+
      |                        ^
      |                        |  upwind
      | downwind               X  LZ
      |                        ^
      v                        |  final
      +------- base ---------->+  <- circuit start is near here
                                     (left-hand circuit shown)
```

1. **To circuit start.** You hear "Landing approach, circuit start, 2 o'clock, 600 metres". The clock position is relative to your direction of travel (12 o'clock = straight ahead). Updates come every 20 s, or sooner if you're more than 30° off. If you're well above circuit height you also hear "Descend to 400 feet".
2. **Upwind.** On arrival you're told to turn into wind if needed ("Turn left now" / "Turn right now").
3. **Crosswind.** At the end of the upwind leg you hear **"Turn crosswind now"**. Turn whichever way you like; the vario detects your choice and flies the rest of the circuit that way. You can fix the direction in settings.
4. **Downwind.** "Turn left/right now" at the end of the crosswind leg.
5. **Base.** The base turn point is recalculated continuously from your height, so that an engine-off glide along base and final ends on the LZ. If you're high, the downwind leg is extended. If you're low you hear "Too low".
6. **Final.** "Turn left/right now", then "Too high" / "Too low" against the glide path.

Each turn call names the leg you are turning onto: "Turn right now, downwind". If you drift more than 30° off a leg for 4 s you hear "Steer left" or "Steer right". It is not said while you are making a turn that was just called: for 5 s after a "Turn ... now" (your reaction time), and for as long as your track keeps swinging towards the leg (the angle off the leg shrinking by 3° or more a second), plus 3 s after it stops. A turn that stalls halfway, goes the wrong way or is ignored is called after that. Guidance ends on landing, once you pass the LZ, or when you press the button to cancel.

**It learns how fast you turn.** The first two turns (to crosswind and to downwind) are called 3 s early (*Call turns early by*) and timed: how long you took to start turning, and how fast you turn. From the base turn on, the calls come early by that much instead: a pilot who takes 3 s for a quarter circle is called about 5 s before the corner, one who takes 20 s about 16 s before it. A turn that was not measured properly is not used, and with fewer than two good ones the setting is kept. Switch it off with *Learn how fast I turn from the first two turns*.

The vario has no wind sensor. Wind direction is taken from your takeoff run (assumed into wind), then refined from GPS whenever you turn through about 270°. Ground speeds measured before a change of wind (straight flight before you start circling) are recognised as not fitting the circle and left out, and two circling estimates in a row that agree are averaged with each other and not with the older estimate, so after a real change of wind the estimate is right from the second full circle. If the windsock tells you otherwise, set the wind manually on the web page (Settings → Wind).

## Emergency glide (hold)

You hear your glide range, the LZ as a clock position and its distance, and "Cannot reach landing zone" if it's beyond your glide. Then:
- **"On course"** once you're pointing at the LZ (within ±15°).
- **"Landing zone, N o'clock"** if you're more than 30° off for 3 s.
- **"Distance …"** every 30 s, with "Cannot reach landing zone" while it's out of range.

Glide range uses the glide ratio and trim airspeed settings, adjusted for the estimated wind.

## The web page

1. On your phone, join the Wi-Fi network **ParaVario-XXXX** (default password **paramotor**), where XXXX is a code unique to your unit (the last four digits of its MAC address), so several varios nearby can be told apart. The same code is in its Bluetooth name.
2. The page usually opens by itself. If it doesn't, open **http://192.168.4.1** in a browser.

The phone has no internet while it's on this network, so turn mobile data on if you need both.

| Section | Contents |
|---|---|
| Banner | Green **Ready to fly**, or amber with what's missing and the satellite count. Hidden while flying, except during guidance: blue **Approach** with the current circuit leg, or red **Emergency** with the glide range. |
| Tiles | Refreshed every second. Top group: height above the LZ (and above sea level) with a rolling graph of the last 10 minutes beside it, vario, ground speed and track, **glide ratio**, flight state and time, LZ distance with an arrow to it, wind as a windsock (the sock streams the way the wind blows), both relative to your direction of travel. Under the Actions: GPS signal quality (strong, good, ok, poor, none) with the satellite count, battery (minutes left when known, otherwise the percentage), engine RPM. The Ground speed tile has a **compass** for your track against absolute north (a ring marked N, E, S, W with an arrow inside). The **Background noise** tile and the RPM confidence are tuning data and are hidden unless you turn on *Show background noise and RPM confidence on the flight tab* (Engine RPM, Advanced). |
| Glide ratio | Ground distance covered per unit of height lost, e.g. `6.7 : 1`. The big value is the average over the last second, the small one the average over the last 60 seconds (both windows are in the Glide settings). Shows `–` when level or climbing, and `>50` above 50. Needs a GPS. |
| Actions | One card headed **Actions**, and which one you see depends on the phase. **In flight:** **Landing support**, **Emergency support**, **Update me** and **Mute vario** (the same as the button). **On the ground:** **Audio test**, **Mute vario**, **Set LZ manually** and **LZ = takeoff**. Below them, **Full screen** (hides the browser's bars). |
| Alerts | A strip under the banner for what a pilot must know in flight: **Battery low** or **critical** (with the minutes left and the distance to the LZ), **Barometer failed**, **No GPS fix**. It clears when the problem goes away. |
| Settings | **Quick settings** first: volumes, units, circuit direction, glide ratio and trim airspeed (also in their groups; the two copies follow each other). Then collapsible groups (see below); Battery, Wi-Fi, GPS, Engine RPM, Wind, Logging and Glide are inside **Advanced settings** at the end. Change a value and it is saved a moment later. Settings marked *restart* apply after a restart. |
| Reset flight state | On the Logs & system tab. The way out if the vario ever thinks it is flying when it is not: it forgets the flight and any guidance. |
| Flight logs | IGC files for each flight. Tap a name to download or Delete to remove. The oldest logs are deleted automatically when space runs low. |
| Logging | Live messages from the vario, with Pause, Clear and Save as file, for finding problems. Opens automatically on the debug firmware. The same lines appear on the USB serial port. (Flight recording is set in the Logging group of the settings.) |
| Firmware update | Shows the installed version. Choose a `.bin` file and tap **Install update** (see *Maintenance*). Disabled while flying. |
| System | Restart, Factory defaults. |
| Help | The **Help** tab opens the guides and the feature reference on GitHub (the latest version; the phone needs internet, so turn mobile data on). Each group on the Settings tab also has **Help** links to its entry in the reference. |

Heights, distances and speeds on the page follow your chosen units, and you always enter them in those units.

**Using the page like an app.** On Android, tap **Full screen** to hide Chrome's bars. On an iPhone, open the page in Safari, tap Share, then **Add to Home Screen**, and open the vario from the new icon: it opens without Safari's bars.

## Settings

Every setting, with its default, an example and what it does underneath, is in [Feature_Reference.md](Feature_Reference.md). A quick map:

| Settings group | Explained in |
|---|---|
| Units (height), Audio (volumes, volume change speed, raise volume with noise) | [Units and Audio](Feature_Reference.md#basic-settings-units-and-audio) |
| Units (distance, speed) | [Advanced: Units and Audio](Feature_Reference.md#adv-settings-units-and-audio) |
| Vario | [Basic: Vario](Feature_Reference.md#basic-settings-vario) |
| Height calls | [Basic: Height calls](Feature_Reference.md#basic-settings-height-calls) |
| Takeoff & landing | [Basic](Feature_Reference.md#basic-settings-takeoff-and-landing) and [Advanced](Feature_Reference.md#adv-settings-takeoff-and-landing) |
| Engine RPM | [Engine RPM](Feature_Reference.md#basic-settings-engine-rpm) |
| Glide, Landing zone, Landing approach, Emergency, Wind | [Advanced](Feature_Reference.md#adv-settings-glide) |
| GPS | [Advanced: GPS](Feature_Reference.md#adv-settings-gps) |
| Bluetooth | [Basic](Feature_Reference.md#basic-settings-bluetooth) and [Advanced](Feature_Reference.md#adv-settings-bluetooth) |
| Wi-Fi | [Basic: Wi-Fi](Feature_Reference.md#basic-settings-wi-fi) |
| Battery | [Basic: Battery](Feature_Reference.md#basic-settings-battery) |
| Logging | [Advanced: Logging](Feature_Reference.md#adv-settings-logging) |

## Maintenance

- **Firmware (USB):** `pio run -e release_usb -t upload`. Use `-e debug_usb` for detailed logging.
- **Firmware (Wi-Fi from VS Code):** connect the PC to the vario's Wi-Fi (or, if you have set the *Server network*, just power the vario on near the PC), then `pio run -e release -t upload` (or `-e debug`). The vario restarts when it is done.
- **Firmware (phone):** build on the PC (`pio run -e release`) and copy `.pio/build/release/firmware.bin` to the phone. On the phone, join the vario's Wi-Fi, open **http://192.168.4.1** in the normal browser (the pop-up captive-portal window often can't pick files), then go to **Logs & system**, choose the file, enter the update password and tap **Install update**. Keep the phone close and the page open until it says *Installed*. A failed or interrupted upload leaves the old firmware running.
- **One program for every vario.** The same `firmware.bin` goes on a vario with and without a GPS module.
- **Voice clips:** edit `tools/vocab.txt` or add your own recordings to `tools/voice_custom/`, run `python tools/make_voice.py`, then `pio run -e release -t uploadfs`. You can also run `pio run -e release -t buildfs` and install `.pio/build/release/littlefs.bin` from the phone page. **This erases the flight logs stored on the vario, so download them first.** Settings are kept.
- Firmware and voice-clip updates are refused while the vario is flying.
- **Factory defaults:** web page, Logs & system tab, *Factory defaults*.

## Hardware test

A separate test firmware in `hardware_test/` checks every part of the hardware without the flight logic.

1. `cd hardware_test`, then `pio run -t upload`. Re-flash the flight firmware from the main folder afterwards.
2. You hear a rising chirp at power-on if the amplifier and speaker work.
3. Join Wi-Fi **ParaVario-Test** (password **paramotor**). The page opens by itself, or go to http://192.168.4.1. USB serial (`pio device monitor`) prints the same results every 5 s.

Each part gets a green, amber or red indicator with a hint: storage and voice clips, I2C bus (lists every address found), barometer (lift the vario about 1 m and watch the vario reading), GPS (bytes, detected baud, NMEA checksums, satellites and fix, plus the raw NMEA lines), battery (pin and battery voltage), push button (state, count, single/double/long), Bluetooth (advertises as *ParaVario-Test* and sends a test `$LK8EX1` every second when FlyGaggle connects) and Wi-Fi.

Audio tests: a volume slider; preset tones, sweeps and a loud square wave; a custom tone (frequency, length, sine or square); a vario simulation slider from −5 to +8 m/s using the same beep pattern as the flight firmware; each voice clip on its own, all clips in turn, or a sample phrase; and an option to beep when the button is pressed.

The test uses the same storage layout as the flight firmware, so flashing it keeps your voice clips, settings and flight logs.
