# ParaVario user guide - Advanced vario (GPS and microphone)

ParaVario is an audio vario and flight computer for paramotor flying. It beeps when you climb, calls out your height above the landing zone (LZ), talks you around a landing circuit, and guides you back to the LZ if the engine stops. It sends vario and GPS data to FlyGaggle over Bluetooth and has a settings page you open on your phone.

This guide is for the **Advanced** vario, which has a GPS receiver and a microphone. A vario without them (the **Basic** vario) has its own guide, [User_Guide_(basic).md](User_Guide_(basic).md). Every tile, button and setting is explained in [Feature_Reference.md](Feature_Reference.md): Part 1 for what both versions have, Part 2 for what the GPS and microphone add. This guide shows how to use the vario; the reference explains what each control does.

The version shown at the bottom of the web page ends in `-advanced` for this vario (`-basic` for the simpler one).

## Before you fly

1. Switch on. You hear a short climb beep at the vario volume (so you can judge the level), then, a second later, **"Ready"**.
2. Wait outside for **"GPS fix"**.
3. Optionally, open the web page on your phone (see [The web page](#the-web-page)). The banner at the top turns green and reads **Ready to fly** once the GPS has enough satellites, the barometer works and the landing zone is known. It shows the satellite count against the minimum, for example `7 / 6 satellites`. The banner is only a guide; whether to fly is your call.
4. Take off. Takeoff is detected automatically and you hear **"Take off"**. The spot where you stood before the takeoff run becomes the landing zone, and its height becomes zero for all height calls. To use a different LZ, see [Landing zone](#landing-zone).

## If a part is missing

A vario built without the GPS or without the microphone (see [User_Guide_(basic).md](User_Guide_(basic).md)) simply leaves out what depends on it:

- **No GPS:** no speed, wind, landing zone, approach or emergency guidance and no flight log; every press of the button mutes or unmutes the vario.
- **No microphone:** the volume stays at the set value, and the Engine RPM and Background noise tiles and the Spectrum tab are hidden.

On a vario that has both, a GPS that stops working in flight leaves you with the vario, heights and height calls; "No GPS" is spoken after 5 seconds.

## Button

| Press | What it does |
|---|---|
| **Single press** | Starts **landing approach** guidance ("Landing approach…"). Press again to cancel ("Approach cancelled"). Does nothing while emergency mode is on. |
| **Double press** | Starts **emergency glide** guidance ("Emergency, glide range…"). Double press again to cancel ("Emergency cancelled"). Starting emergency mode cancels any landing approach. |
| **Long press** (hold about 1 s) | Mutes or unmutes the **vario beeps** ("Vario muted" / "Vario on"). Spoken calls are never muted. |

A single press takes effect about half a second after you let go, because the vario waits to see whether a second press follows. (On a vario without a GPS every press mutes.)

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
| "400 feet" | Every 60 s while flying (current height). |
| "Flight time 1 hour 5 minutes" | Every 5 minutes; skipped during approach or emergency guidance. |
| "Landed, flight time …" | Landing detected. |
| "No GPS" | GPS lost for 5 s in flight. |
| "Battery low" | Battery below the warning voltage; repeats every 5 minutes. |

All heights are above the landing zone, not above the ground you're currently flying over.

## Landing approach (single press)

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

If you drift more than 30° off a leg for 4 s you hear "Steer left" or "Steer right". Guidance ends on landing, once you pass the LZ, or when you press the button to cancel.

The vario has no wind sensor. Wind direction is taken from your takeoff run (assumed into wind), then refined from GPS whenever you turn through about 270°. If the windsock tells you otherwise, set the wind manually on the web page (Settings → Wind).

## Emergency glide (double press)

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
| Tiles | Refreshed every second. Top group: height above the LZ (and above sea level) with a rolling graph of the last 10 minutes beside it, vario, ground speed and track, **glide ratio**, flight state and time, LZ distance and bearing, wind. Under the Actions: GPS satellites and signal quality (strong, good, ok, poor, none), battery, engine RPM, background noise. |
| Glide ratio | Ground distance covered per unit of height lost, e.g. `6.7 : 1`. The big value is the average over the last second, the small one the average over the last 60 seconds (both windows are in the Glide settings). Shows `–` when level or climbing, and `>50` above 50. Needs a GPS. |
| Actions | Landing approach, Emergency glide, Mute vario (same as the button), **Test voice**, **Set LZ here**, **LZ = takeoff**, **Full screen** (hides the browser's bars). |
| Settings | Collapsible groups (see below). Change a value and it is saved a moment later. Settings marked *restart* apply after a restart. |
| Flight logs | IGC files for each flight. Tap a name to download or Delete to remove. The oldest logs are deleted automatically when space runs low. |
| Logging | Live messages from the vario, with Pause, Clear and Save as file, for finding problems. Opens automatically on the debug firmware. The same lines appear on the USB serial port. (Flight recording is set in the Logging group of the settings.) |
| Firmware update | Shows the installed version. Choose a `.bin` file and tap **Install update** (see *Maintenance*). Disabled while flying. |
| System | Restart, Factory defaults. |
| Help | The **Help** tab opens the guides and the feature reference on GitHub (the latest version; the phone needs internet, so turn mobile data on). Each group on the Settings tab also has **Help** links to its entry in the reference. |

Heights, distances and speeds on the page follow your chosen units, and you always enter them in those units.

## Settings

Every setting, with its default, an example and what it does underneath, is in [Feature_Reference.md](Feature_Reference.md). A quick map:

| Settings group | Explained in |
|---|---|
| Units, Audio (volumes, volume change speed) | [Basic: Units and Audio](Feature_Reference.md#basic-settings-units-and-audio) |
| Audio (raise volume with noise) | [Advanced: Units and Audio](Feature_Reference.md#adv-settings-units-and-audio) |
| Vario | [Basic: Vario](Feature_Reference.md#basic-settings-vario) |
| Height calls | [Basic: Height calls](Feature_Reference.md#basic-settings-height-calls) |
| Takeoff & landing | [Basic](Feature_Reference.md#basic-settings-takeoff-and-landing) and [Advanced](Feature_Reference.md#adv-settings-takeoff-and-landing) |
| Engine RPM | [Advanced: Engine RPM](Feature_Reference.md#adv-settings-engine-rpm) |
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
- **Install the right program.** The version ends in `-advanced` for this vario. An `-basic` program does not use the GPS or microphone.
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
