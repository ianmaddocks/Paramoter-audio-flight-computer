# ParaVario feature reference

Every tile, button, sound and setting, explained. It applies to both versions of the vario:

- **Basic**: barometer and speaker only (no GPS, no microphone). See [User_Guide_(basic).md](User_Guide_(basic).md).
- **Advanced**: adds the GPS receiver and the microphone. See [User_Guide_(advanced).md](User_Guide_(advanced).md).

**Part 1** covers everything a Basic vario has, and an Advanced vario has it too. **Part 2** covers what the GPS and the microphone add; each of those features is marked *Advanced Feature*. On a vario without that part, the feature is simply not shown on the page.

Heights in the examples use the default unit (feet). You can switch to metres in Settings.

**Contents**

- [Part 1 - Basic](#part-1---basic)
  - [Tiles](#basic-tiles) · [Banner](#basic-banner) · [Buttons and actions](#basic-buttons) · [Sounds](#basic-sounds)
  - [Settings: Units and Audio](#basic-settings-units-and-audio) · [Vario](#basic-settings-vario) · [Height calls](#basic-settings-height-calls) · [Takeoff and landing](#basic-settings-takeoff-and-landing) · [Bluetooth](#basic-settings-bluetooth) · [Wi-Fi](#basic-settings-wi-fi) · [Battery](#basic-settings-battery)
  - [Logs and system tab](#basic-system) · [Help tab](#basic-help)
- [Part 2 - Advanced](#part-2---advanced)
  - [Tiles](#adv-tiles) · [Banner](#adv-banner) · [Buttons and actions](#adv-buttons) · [Guidance](#adv-guidance) · [Sounds](#adv-sounds) · [Spectrum tab](#adv-spectrum)
  - [Settings: Units and Audio](#adv-settings-units-and-audio) · [Engine RPM](#adv-settings-engine-rpm) · [Takeoff and landing](#adv-settings-takeoff-and-landing) · [Glide](#adv-settings-glide) · [Landing zone](#adv-settings-landing-zone) · [Landing approach](#adv-settings-landing-approach) · [Emergency](#adv-settings-emergency) · [Wind](#adv-settings-wind) · [GPS](#adv-settings-gps) · [Bluetooth](#adv-settings-bluetooth) · [Logging](#adv-settings-logging)

Settings are changed on the Settings tab of the web page. A change is saved a moment after you make it. A setting marked **restart** takes effect after the vario is restarted.

---

# Part 1 - Basic

<a id="basic-tiles"></a>
## Tiles (Flight tab)

### Height AGL
How high you are above where you took off.

After you take off from the field the tile starts at 0 ft and climbs as you do, for example `328 ft`. The small line under it, such as `1450 ft MSL`, is the altitude above sea level.

"AGL" means above ground level. The vario has no way to see the ground, so it uses the spot you were standing on before the climb as zero. While you stand still it follows slow weather changes in air pressure, so it stays at 0. It stops following the moment you start climbing or sinking (see *Ground level held when climbing or sinking faster than*). The sea-level figure is the barometer's pressure altitude on a standard-pressure day, so on a Basic vario it can differ from the true altitude by the weather of the day. Advanced varios correct it from the GPS.

### Height graph
A small graph of your height over the last 10 minutes.

Look at it to see how your climb or descent has gone: a steady slope up means a steady climb, a flat line means level flight. The newest height is at the right edge; the number at top left is the highest point shown.

The vario itself remembers the last 10 minutes (one point every 2 seconds), so the graph is complete as soon as you open the page, even if you opened it only now. The 10 minute span is fixed.

### Vario
How fast you are climbing (+) or sinking (-), in metres per second.

For example, `+1.5 m/s` means you gain 1.5 metres every second. `-2.0 m/s` means you lose two. A small `muted` under it means the beeps are switched off.

The number comes from the barometer, smoothed so it does not jump about. How quickly it reacts is set by *Responsiveness*.

### Flight time
Whether you are flying, and for how long.

While you fly the tile shows the elapsed time, for example `12:34`, with `Flying` under it. On an Advanced vario `· logging` is added while a flight log is being recorded. On the ground it says `Grounded`, and after a flight the line under it shows the length of that flight, for example `last flight 35:10`.

Takeoff and landing are detected automatically (see *Takeoff and landing* settings). The flight time starts at takeoff and stops at landing.

### Battery
How full the battery is, and roughly how long it will last.

For example, `74%`, and under it `3.96 V · ~3 h 20 min`. When the vario is on USB power the tile says `USB`.

The percentage comes from the battery voltage. After the vario has once been run until the battery was flat, it learns the real discharge curve of your battery and the percentage and minutes left become more accurate. See the *Battery* settings.

<a id="basic-banner"></a>
## Banner (top of the Flight tab)

### Ready to fly
A coloured bar that says whether the vario is ready.

Green `Ready to fly` means go. Amber shows what is still missing, for example `Vario settling` for the first few seconds after power-on. The banner is a guide only; whether to fly is your decision. It disappears in flight.

A Basic vario is ready as soon as the barometer works and has settled. (An Advanced vario also waits for the GPS.) If the barometer is not detected the banner says `Barometer not working`.

<a id="basic-buttons"></a>
## Buttons and actions

### Push button
The one button on the vario.

On a Basic vario **any press** switches the vario beeps off or on again. You hear "Vario muted" or "Vario on" to confirm.

Spoken calls are never muted, only the beeps. (On an Advanced vario the button does more; see Part 2.)

### Mute vario (page button)
The same as the push button.

Tap it to silence the beeps while you chat or hold the vario; tap again (it reads *Unmute vario*) to bring them back.

### Full screen
Hides the browser's bars so the page fills the phone screen.

Tap **Full screen** at the bottom of the Actions and the address bar and the phone's own bars disappear, much as they would in an installed app. The button then reads **Exit full screen**; you can also swipe down from the top edge. You have to tap it each time you open the page, because a browser only allows full screen after a tap. If the browser does not support full screen the button is not shown. That is the case on an iPhone, where Safari does not allow it; there, add the page to the Home Screen instead (Share, then **Add to Home Screen**) and open the vario from its icon: the page then opens without Safari's bars, and the Full screen button is not needed (it is hidden when the page runs that way).

### Test voice
Plays a sample announcement so you can check the speaker and the volume.

Tap it on the ground and listen. It says "System ready, 500 feet, 3 o'clock, 1.3 kilometres". If it is too loud or quiet, change *Speech volume*.

<a id="basic-sounds"></a>
## Sounds you hear

### Start-up beep and "Ready"
A beep when you switch on, then "Ready".

You hear a short climb beep, then a second later "Ready". The beep is at your vario volume on purpose: it lets you judge how loud the beeps will be in flight.

### Climb beeps
Beeps that tell you you are going up.

The stronger the lift, the higher the pitch and the faster the beeps. A gentle +0.5 m/s gives slow low beeps; +4 m/s gives fast high ones.

Beeps start above the *Climb threshold*. The pitch is *Base tone* plus *Tone rise per m/s* for every m/s of climb. Beeps pause while the vario is speaking.

### Sink alarm
A steady low tone when you are sinking fast.

Sinking faster than 2 m/s gives a continuous low tone that gets lower the faster you sink. It is a warning, so it cannot be missed.

It starts below the *Sink alarm threshold*.

### Height calls
The vario says your height as you pass set heights.

For example, climbing through 250 feet you hear "250 feet". Descending through 100 feet you hear "100 feet". Every minute in flight it also tells you your current height.

Which heights are announced is set in the *Height calls* settings. Heights are above the point you took off from.

### "Take off" and "Landed"
The vario tells you when it has noticed takeoff and landing.

You hear "Take off" when you leave the ground and "Landed, flight time 35 minutes" once you are down.

### Flight time calls
The elapsed flight time, spoken every few minutes.

It is spoken as "Flight time 1 hour 5 minutes", by default every 5 minutes.

### Battery low
A warning when the battery is getting flat.

You hear "Battery low" when the voltage falls below *Low battery warning*. It repeats every 5 minutes until you land and charge.

<a id="basic-settings-units-and-audio"></a>
## Settings: Units and Audio

### Height (unit)
Feet or metres.

Choose what you are used to. Everything shown on the page and every height spoken uses it, and the heights you type in settings (such as the height calls) are entered in the same unit. Default: Feet.

### Speech volume
How loud the spoken calls are.

The scale runs from 0 (silent) to 100 (as loud as it goes). Use *Test voice* while adjusting. Default: 90 %.

### Vario volume
How loud the beeps are.

Set it so the beeps are clear over your engine noise or wind but not painful. The start-up beep is at this volume, so it is a quick way to check. Default: 60 %.

### Fastest volume change
Stops the volume from changing suddenly.

If you change the volume (or, on an Advanced vario, the noise around you changes), the sound moves to the new volume smoothly instead of jumping. A lower number is gentler. Default: 40 % per second.

At 40 %/s, going from 10 % to 100 % takes about 2 to 3 seconds. The step is also spread over a tenth of a second inside the audio output, so there is never a click.

<a id="basic-settings-vario"></a>
## Settings: Vario

### Vario beeps enabled
The master switch for the beeps.

Turn off to have no beeps and no sink alarm at all. Spoken calls are not affected. Default: On.

### Only when flying
Keep the beeps off on the ground.

With this on, the vario is silent while you walk to the launch and during the takeoff run, and starts once takeoff has been detected. On a Basic vario that is after you have climbed about 50 feet. Turn it off to hear beeps all the time, for example to test the vario by lifting it. Default: On.

### Climb beeps
Switch the climb beeps on or off.

Turn off if you want only the sink alarm. Default: On.

### Sink alarm
Switch the sink warning tone on or off.

Turn off if you find it distracting while descending deliberately. Default: On.

### Climb threshold
How fast you must climb before the beeps start.

For example, at 0.3 m/s the beeps start once you climb faster than 0.3 m/s. Raise it to ignore weak lift; lower it to hear every hint of it. Default: 0.3 m/s.

### Sink alarm threshold
How fast you must sink before the sink tone starts.

For example, at -2.0 m/s, sinking faster than 2 m/s starts the tone. Closer to zero (-1.0) warns sooner. Default: -2.0 m/s.

### Base tone
The pitch of the beeps at the weakest lift.

Raise it if low tones are hard to hear over the engine, lower it if a high tone tires you. Default: 700 Hz.

### Tone rise per m/s
How much the pitch rises as the lift gets stronger.

For example, at 100 Hz per m/s, +3 m/s of climb is 300 Hz higher than the base tone. Set it higher to make strong lift stand out more. Default: 100 Hz.

### Responsiveness
How quickly the vario reacts, against how steady it is.

Higher reacts faster but the beeps can be jumpy. Lower is smoother but a little late. Try small steps and fly a little. Default: 1.0.

This sets how much the barometer filter expects your vertical speed to change from moment to moment. A higher value lets it follow quick changes and also passes more noise.

### Settle time at power-on
How long the vario waits after power-on before it starts beeping.

The barometer takes a few seconds to warm up, and during that time the reading wanders, which would make false beeps. The vario stays silent until the reading has been steady for this long. The banner says `Vario settling` meanwhile. Default: 5 s. Set 0 to start at once.

If it never gets steady (for example you restarted it in the air) it starts anyway after three times this period.

<a id="basic-settings-height-calls"></a>
## Settings: Height calls

### Announce when climbing / Announce when descending
Switch the height calls on the way up and on the way down on or off separately.

Many pilots want calls on the way down (to the landing) and fewer on the way up. Default: both on.

### Boundaries when climbing / Boundaries when descending
The heights at which the vario speaks.

A list separated by commas, in your height unit. Climbing default: `50, 100, 250, 500, 750, 1000`. Descending default: `50, 100, 150, 250, 350, 500, 750, 1000`. To hear every 100 feet on the way down, type `100, 200, 300, 400, 500`.

The climbing list is only used on the way up and the descending list on the way down. Each is greyed out while its *Announce* switch is off.

### Repeat every (above last boundary)
Keeps the calls coming above your highest listed height.

For example, with a last boundary of 1000 ft and this at 500 ft you also hear "1500 feet", "2000 feet", and so on. 0 means no calls above the list. Default: 500 ft.

### Hysteresis
Stops the same call repeating when you hover at a boundary.

For example, with 10 ft, climbing through 250 ft is announced when you reach 260 ft, and it will not be announced again unless you drop to 240 ft first. Default: 10 ft.

### Periodic height every
Says your height now and then, even between boundaries.

For example, at 60 s you hear your height each minute: "400 feet". A boundary call restarts the timer. 0 turns it off. Default: 60 s.

This call is low priority: if the vario is already speaking, it is skipped rather than queued.

### Flight time every
How often the flight time is announced.

For example, at 5 min you hear "Flight time 15 minutes". 0 turns it off. Default: 5 min.

<a id="basic-settings-takeoff-and-landing"></a>
## Settings: Takeoff and landing

### Takeoff height gain without GPS
How far you must climb for the vario to decide you have taken off.

For example, at 49 ft (15 m), once you have climbed 49 ft from where you stood, you hear "Take off" and the flight starts. Lower it to detect takeoff sooner, but not so low that carrying the vario upstairs counts. Default: 49 ft.

An Advanced vario with a good GPS fix uses speed instead (see Part 2); this setting is also its fall-back when the GPS has no usable fix.

### Ground level held when climbing or sinking faster than
The climb rate above which the vario stops treating your height as "ground".

On the ground the vario's zero follows slow pressure changes so your height reads 0 ft all morning. When you start to climb faster than this it freezes the zero, so the height is measured from where you actually left the ground. Default: 0.5 m/s.

Without this, the zero would creep upwards with a slow climb and the takeoff would be missed. Set it above the speed at which the weather changes the reading (about 0.1 m/s) and below your normal initial climb.

### Landed after
How long you must be still before the vario decides you have landed.

On a Basic vario "still" means the barometer reads a steady height, and it needs twice this time: with 30 s, 60 s of steady height is declared a landing and you hear "Landed, flight time ...". Default: 30 s.

Flying level at a constant height also reads as steady, but you will rarely stay perfectly steady that long. If you fly very level for a minute and get a false "Landed", increase this. After a false landing the vario needs another climb of the takeoff height gain before it notices you are flying again.

<a id="basic-settings-bluetooth"></a>
## Settings: Bluetooth

### Device name
The name your phone or app sees when it looks for the vario.

While it stays at the default `ParaVario` the vario adds a code taken from its unique hardware address, for example `ParaVario-12B8`, so two varios nearby can be told apart. Type a name of your own and it is used exactly as typed. Restart to apply. Default: ParaVario.

### Vario sentences rate
How often vario data is sent over Bluetooth.

A rate of 5 times a second suits FlyGaggle. A lower rate saves a little radio traffic. Default: 5 Hz.

The data is sent in the common `$LK8EX1` and `$LXWP0` formats, which FlyGaggle and many other apps understand.

<a id="basic-settings-wi-fi"></a>
## Settings: Wi-Fi

### Network name
The name of the Wi-Fi network the vario creates.

Join it with your phone to open the web page. Like the Bluetooth name, the default `ParaVario` becomes `ParaVario-XXXX` with a code unique to your vario. Restart to apply.

### Password
The password for that network.

It must be at least 8 characters. Leave it blank for an open network (not recommended). Restart to apply. Default: paramotor.

### Server network name / Server network password
Lets the vario also join your home Wi-Fi, so a computer there can reach it.

Enter your home router's name and password. When the vario is switched on at home it joins that network as well, and you can open `http://paravario.local` from your computer and update the firmware without changing your computer's Wi-Fi. The vario's own network stays on. Leave the name blank to turn this off. Restart to apply.

Only 2.4 GHz networks work. While the vario is searching for your network, its own network can be less reliable (it has one radio), which is why it gives up after a while (next setting).

### Stop searching if server network not found after
How long the vario looks for your home network before giving up.

Away from home the vario would otherwise keep searching, which makes its own network unreliable. After this time (30 s) it stops. A restart tries again. 0 keeps trying. Default: 30 s.

<a id="basic-settings-battery"></a>
## Settings: Battery

### Divider ratio
A fixed number that matches the vario's battery-measuring circuit.

Leave it at 2.0 unless you have changed the resistors on the battery divider.

### Calibration factor
A fine adjustment so the voltage shown matches a voltmeter.

If the vario shows 4.00 V but a meter on the battery says 4.10 V, set it to 1.025 (4.10 divided by 4.00). Default: 1.0.

### Low battery warning
The voltage at which "Battery low" is spoken.

For example, at 3.5 V you get the warning with some capacity still in reserve. Raise it for more notice. Default: 3.5 V.

### Voltage shown as 100%
The voltage you treat as "full".

A freshly charged battery reads about 4.1 V and drops quickly to about 4.0 V, so 4.09 V is called 100 %. Never shows more than 100 %. Default: 4.09 V.

### Learn discharge to estimate minutes left and refine %
Lets the vario learn how long your battery really lasts.

Leave it on. Once, run the vario from full until the battery is flat, then charge it; from then on the minutes left and the percentage are based on your battery, not a typical one.

The vario notes the time taken to fall through each 50 mV step from 4.10 V to 3.30 V. When a discharge ends with the battery running down and is followed by a recharge, those timings are blended into a stored table (the newest discharge counts 30 %). It never learns from a battery that was charged before it ran down, and it stays quiet while charging.

### Runtime on a full battery, used until learned
Your estimate of how long a full battery lasts, until the vario has measured it.

If you usually get about 6 hours, leave it at 360 minutes. Change it to your own experience so the first estimates are sensible. Default: 360 min.

<a id="basic-system"></a>
## Logs and system tab

### Flight logs
The list of recorded flights.

Flight logs (IGC files) are made only when a GPS is fitted, so a Basic vario shows "No flights recorded yet".

### Logging
A live view of what the vario is doing, useful when something does not work as expected.

Open the Logging section and the vario's own messages scroll past. *Pause* freezes them, *Clear* empties the list, and *Save as file* keeps a copy to send to someone who is helping you. (This is not the flight log, which is the list of recorded flights above, and it is not the *Logging* group of settings, which controls flight recording.) The section opens by itself on a debug build.

### Firmware update
Installs a new program on the vario from your phone or computer.

Choose the `firmware.bin` file, type the update password, tap *Install update*, and keep the page open until it says *Installed*. The vario restarts. If the upload is interrupted, the old program keeps running. It is not possible while flying.

The version shown ends in `-basic` or `-advanced` so you can see which kind of program is installed. Install the one that matches your hardware.

### Restart / Factory defaults
Restart the vario, or put every setting back to its starting value.

*Restart* is needed after changing settings marked "restart". *Factory defaults* asks first, then resets all settings and restarts. Flight logs and the learned battery curve are kept.

<a id="basic-help"></a>
## Help tab

### Help tab
The guides, one tap away.

The Help tab opens the user guide for this vario and this feature reference, with shortcuts to the sections that cover the tiles, buttons, sounds and settings this vario has.

The guides are fetched from GitHub, always in their latest version, and shown inside the page, so the phone needs internet access. A phone joined to the vario's Wi-Fi has none, so turn mobile data on, or leave the vario's network for a moment. The latest guides may describe features that a vario with older firmware does not have yet.

### Help links on the settings
Open a group of settings and a **Help** link appears at the right-hand end of its title.

Tap it to show the part of this reference that explains that group. Where a group has more to explain on this vario (for example the Audio group when a microphone is fitted), a second link, **More help**, follows the first.

---

# Part 2 - Advanced

These features need the GPS receiver, the microphone, or both, and are marked *Advanced Feature*. Where it is not obvious, the text says which part is used.

<a id="adv-tiles"></a>
## Tiles (Flight tab)

### Ground speed
*Advanced Feature*

How fast you are moving over the ground, as measured by the GPS.

For example, `43 km/h` with `track 90°` under it, which means you are travelling due east. Change the unit in Settings (km/h, mph or knots).

With a weak GPS signal the speed is unreliable (it can show 40 km/h while standing still), so below 6 satellites or with poor accuracy the tile shows 0 rather than a false number.

### Glide ratio
*Advanced Feature*

How far you are travelling forward, measured by the GPS, for each unit of height you are losing.

For example, `6.7 : 1` means for every 1 m you sink you move forward 6.7 m. A higher number is a flatter glide, so you can go further. With the engine off, compare it with the glide ratio setting to see whether you will reach the field. Under it, `60 s avg 6.4 : 1` is the longer-term figure. When you are climbing or flying level there is no glide, so the tile shows `–`.

The big number averages the last second and moves quickly; the small one averages the last minute and is steadier. Both windows can be changed in the Glide settings. The ratio is ground speed divided by sink rate, each averaged over the window, so it already includes the wind: a headwind lowers it, a tailwind raises it. The minute average includes any climb in that minute, so after a thermal it shows the overall figure. Above 50 the tile shows `>50`. It only counts in flight, with a good GPS fix.

### Landing zone
*Advanced Feature*

How far away the landing zone (LZ) is, and in which direction.

For example, `620 m` with `85° · 330 ft MSL` under it: the LZ is 620 metres away on a bearing of 85° (nearly east). Until a landing zone is set it says `not set`.

By default the LZ is the spot where you stood before taking off. You can pick a different one (see *Set LZ here*).

### Wind
*Advanced Feature*

Where the wind comes from and how strong it is.

For example, `270°` with `18 km/h · takeoff` under it means a west wind. The word after the dot says how it was worked out: *takeoff*, *circling* or *manual*. Before any estimate it says `unknown`.

The vario has no wind sensor. It first assumes you took off into the wind (your takeoff run direction), then improves the estimate whenever you turn through about 270° by watching how your ground speed changes around the circle. If you know better, set the wind by hand in the Wind settings.

### GPS
*Advanced Feature*

How many satellites the GPS can see and how good the signal is.

For example, `10 sats` with `Fix · signal strong`. `Searching` means no position yet; `no data` means the GPS is not talking to the vario. Signal reads strong, good, ok or poor.

More satellites and a lower HDOP (a measure of how spread-out they are) mean better accuracy. See *Ready to fly: minimum satellites*.

### Engine RPM
*Advanced Feature*

The engine speed, worked out from the sound the microphone hears.

For example, `5400 rpm`. It says `off` when no engine sound is recognised. Under it, `est 5400 · conf 6.2` is the raw estimate and how sure the vario is of it.

The microphone hears the engine's firing frequency and its harmonics, and the vario finds the pattern. If the *conf* number is low the reading is not shown. Tune it in the Engine RPM settings.

### Background noise
*Advanced Feature*

How loud it is around the vario, as heard by the microphone.

For example, `-38 dBFS` with `vario vol 72%` under it: that is the noise level and the volume the vario beeps are currently played at. Use these numbers to set *Noise for set volume* and *Noise for full volume*.

dBFS is a scale where 0 is the loudest the microphone can measure and about -90 is silence. Louder noise is a bigger number (closer to zero). The vario ignores its own speaker, so it does not turn itself up.

<a id="adv-banner"></a>
## Banner

### Ready to fly (with GPS)
*Advanced Feature*

Also waits for the GPS and the landing zone.

The bar shows what is missing: `GPS not responding`, `Waiting for satellites` (with `4 / 6 satellites`), `GPS accuracy low`, `GPS position unstable`, `Vario settling`, or `Landing zone not set`. It turns green when all is well.

### Approach and Emergency banners
*Advanced Feature*

The bar changes colour while guidance is running.

Blue `Approach` with the current circuit leg, or red `Emergency` with your glide range. Both show even in flight.

<a id="adv-buttons"></a>
## Buttons and actions

### Push button (with GPS)
*Advanced Feature*

One button, three kinds of press.

*Single press*: start landing approach (press again to cancel). *Double press*: start emergency glide (double press again to cancel). *Long press* (hold about a second): mute or unmute the beeps.

A single press takes effect about half a second after you let go, because the vario waits to see if a second press follows. A press while emergency guidance is running does nothing for approach; starting an emergency cancels any approach.

### Landing approach / Emergency glide (page buttons)
*Advanced Feature*

The same as the single and double press.

Tap to start, tap again to stop. The button lights up while active.

### Set LZ here
*Advanced Feature*

Makes the spot where you are standing the landing zone.

Walk to the middle of the field you want to land in and tap it; you hear "Landing zone set". Now it stays the LZ until you change it. It needs a GPS fix, otherwise it says there is none.

It also switches *Landing zone* to *Manual* and stores your position and height.

### LZ = takeoff
*Advanced Feature*

Goes back to using your takeoff point as the landing zone.

Tap it after a trip to a different field, to make the next takeoff point the LZ again.

<a id="adv-guidance"></a>
## Guidance

### Landing approach
*Advanced Feature*

Talks you around a landing circuit to the landing zone.

Press once near the end of your flight. You hear "Landing approach, circuit start, 2 o'clock, 600 metres". Head that way. At each corner you hear "Turn crosswind now", "Turn left now" and so on. On final you hear "Too high" or "Too low" against the path to the LZ. If you drift off the leg you hear "Steer left" / "Steer right". Pressing the button again cancels it.

The circuit is built around the LZ: upwind and final are flown into the wind along the same line, upwind passing over the LZ.

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

Stages: (1) to circuit start, with clock-position updates (12 o'clock is straight ahead) and "Descend to 400 feet" if you are high; (2) upwind, turning into wind if needed; (3) crosswind: you choose which way to turn and the vario follows your choice (or fix it in the settings); (4) downwind; (5) base: the turn point is recalculated all the time from your height so that an engine-off glide along base and final lands on the LZ - if you are high the downwind leg is extended, if you are low you hear "Too low"; (6) final. Guidance ends when you land, pass the LZ, or cancel.

### Emergency glide
*Advanced Feature*

Tells you whether you can glide to the landing zone, and steers you there.

Double press if the engine stops. You hear your glide range, the LZ as a clock position and distance, and "Cannot reach landing zone" if it is beyond your range. Then "On course" once you point at the LZ, or "Landing zone, 10 o'clock" if you wander off, and the distance every 30 seconds.

The range comes from your height, the *Glide ratio (engine off)*, and the *Trim airspeed*, adjusted for the estimated wind (a headwind shortens it).

<a id="adv-sounds"></a>
## Sounds you hear

### "GPS fix"
*Advanced Feature*

Said once, when the GPS first finds your position.

### "No GPS"
*Advanced Feature*

Said if the GPS position is lost for 5 seconds in flight.

Height and vario carry on from the barometer, but speed, wind and guidance are unavailable until the fix returns.

### Takeoff and landing calls
*Advanced Feature*

With a GPS, takeoff is spotted from speed.

You hear "Take off" when you have been running at takeoff speed for a few seconds and climbed a little.

<a id="adv-spectrum"></a>
## Spectrum tab

### Microphone spectrum
*Advanced Feature*

A graph of the sounds the microphone hears, by pitch.

Open the tab with the engine running. The tall spikes are the engine and its harmonics; use it to check the engine is picked up and to see how loud the noise is. The dashed line is the vario's own beep. *Pause* freezes the graph; *Floor* sets how deep it shows (down to -100 dBFS).

The graph only works while the tab is open, so it costs nothing in flight.

<a id="adv-settings-units-and-audio"></a>
## Settings: Units and Audio

### Distance (unit)
*Advanced Feature*

Metres (and kilometres) or feet (and miles).

Distances to the landing zone and in guidance are spoken and shown in this unit. Default: Metres / km.

### Speed (unit)
*Advanced Feature*

Kilometres per hour, miles per hour or knots, for the page.

Choose the one you think in; the Ground speed tile follows it. Takeoff and landing speeds are typed in this unit. Default: km/h.

### Raise volume with background noise
*Advanced Feature*

The vario gets louder when it is noisy around you (the microphone listens for the noise).

On the ground it plays at your set volume. With the engine running it climbs towards full volume so you can still hear it. It never goes below your set volume. Default: On.

Between the two noise levels you set (below), the volume rises in proportion. The change is always gradual (see *Fastest volume change*).

### Noise for set volume (see Background noise tile)
*Advanced Feature*

The noise level at or below which the normal volume is used.

Read the Background noise tile when it is quiet (for example -45 dBFS) and enter that. Default: -45 dBFS.

### Noise for full volume
*Advanced Feature*

The noise level at which the volume reaches 100 %.

Read the tile with the engine at full power in flight (for example -15 dBFS) and enter that. Halfway between the two noise levels you get half-way between your set volume and 100 %. Default: -15 dBFS.

### Volume fall time after noise drops
*Advanced Feature*

How slowly the volume comes back down after a loud moment.

A short burst of noise (a gust, a shout) should not make the volume go up and down. With 5 s it takes about 5 s to follow the noise down. Default: 5 s.

The volume rises with the noise within about a second, but only falls at this slower pace.

<a id="adv-settings-engine-rpm"></a>
## Settings: Engine RPM

*Advanced Feature*

These settings tune the engine speed reading that is made from the microphone.

### Sound pulses per revolution
How many engine firings the microphone hears for each turn of the engine.

A two-stroke engine fires once per turn: leave it at 1. A four-stroke fires every other turn: 0.5. Default: 1.

### Lowest RPM / Highest RPM
The range of speeds the vario looks for.

If your engine idles at 2000 rpm and revs to 8000, set about 1500 and 9000. A narrow range avoids mistaking other sounds for the engine. Defaults: 1200 and 10000.

### Minimum confidence
How sure the vario must be before it shows an RPM.

Watch the *conf* number under the Engine RPM tile with the engine running. Set this a little below it. Higher avoids false readings but may drop real ones. Default: 3.

<a id="adv-settings-takeoff-and-landing"></a>
## Settings: Takeoff and landing

*Advanced Feature*

### Takeoff ground speed
The speed at which the vario starts to think you are taking off.

For example, at 15 km/h, your takeoff run has to exceed that. Default: 15 km/h.

### Takeoff speed held for
How long that speed must be kept.

For example, at 5 s, a quick sprint does not count; a proper run does. Default: 5 s.

### Takeoff height gain
How much you must also climb, as well as the speed.

For example, at 33 ft (10 m), "Take off" is announced once you have held takeoff speed and risen that far. Default: 33 ft.

### Landed by ground speed
The ground speed below which you count as stopped, so you have landed.

For example, at 5 km/h, walking pace or slower. Default: 5 km/h.

With a GPS, landing is declared when your ground speed stays below this for *Landed after* seconds (shown in Part 1; it is half the time needed without a GPS).

<a id="adv-settings-glide"></a>
## Settings: Glide

*Advanced Feature*

### Glide ratio tile: average over / second average over
The two time windows for the Glide ratio tile.

The big number averages over the first (default 1 s), the small one over the second (default 60 s). Make the first 3 to 5 s for a calmer number in turbulence. Allowed: 1 to 10 s and 10 to 120 s.

### Glide ratio (engine off)
How far your wing glides, with the engine off, for each unit of height.

A typical paraglider-style wing is about 5 to 8 :1 with a paramotor and the engine off. Enter what you know; it is used for the emergency glide range and the landing approach. Default: 5:1.

If you are not sure, measure it: glide with the engine off in still air and read the Glide ratio tile, or use a lower figure to be safe.

### Trim airspeed (engine off)
Your wing's speed through the air with the engine off.

For many wings this is about 38 km/h. It is used with the glide ratio and the wind to work out the glide range. Default: 38 km/h.

Once the vario has measured the wind by circling, it replaces this with the measured airspeed.

<a id="adv-settings-landing-zone"></a>
## Settings: Landing zone

*Advanced Feature*

### Landing zone (mode)
Where the landing zone comes from: your takeoff point, or a place you set.

*Takeoff point* captures the spot automatically before each takeoff. *Manual* uses the position stored below. Default: Takeoff point.

### Latitude / Longitude / Elevation
The stored position and height of the manual landing zone.

Easiest is to stand on the field and tap *Set LZ here*, which fills all three in. Typing them is only needed if you know the coordinates.

<a id="adv-settings-landing-approach"></a>
## Settings: Landing approach

*Advanced Feature*

These settings shape the landing circuit.

### Circuit direction
Left-hand or right-hand circuit, or your choice on the day.

*Pilot's choice* lets you pick at the first turn. *Left hand* or *Right hand* fixes it, for example to match the field's rule. Default: Pilot's choice.

### Circuit height
The height at which you should be flying the circuit.

For example, at 400 ft, if you arrive far above it you hear "Descend to 400 feet". Default: 400 ft.

### Upwind leg / Crosswind leg / Downwind leg (target)
How long each leg of the circuit is.

Defaults are 400 m each. Shorten them for a small field. The crosswind figure is also the length of the base leg; the downwind leg is made longer automatically if you are high.

### Start final at
The height you want to have when you turn onto final.

For example, at 164 ft (50 m) you should be that high as you start the last straight approach. Default: 164 ft.

### Minimum final length
The shortest final approach the vario will plan.

A minimum of 100 m keeps a little straight run before touchdown. Default: 100 m.

### Circuit start capture radius
How close to the start point counts as "arrived".

For example, at 80 m, once you are within 80 m of the circuit start the vario moves on to the upwind leg. Default: 80 m.

### Call turns early by
Warns you of a turn a moment before the corner.

For example, at 3 s you hear "Turn left now" 3 seconds before you reach the corner, giving time to react and start the turn. Default: 3 s.

### Off-heading warning
How far off a leg's heading you may drift before being told.

For example, at 30°, "Steer left" or "Steer right" is said if you are off by more than 30° for a few seconds. It is also the limit used in emergency glide. Default: 30°.

### Guidance update every
How often distance and direction are repeated on the way to the circuit start.

For example, at 20 s you hear it every 20 s (sooner if you are well off course). Default: 20 s.

<a id="adv-settings-emergency"></a>
## Settings: Emergency

*Advanced Feature*

### Distance update every
How often the distance to the landing zone is repeated.

The default is every 30 s. Shorter gives more frequent calls but more talking. Default: 30 s.

### On-course tolerance
How close to straight at the LZ counts as "On course".

For example, at 15°, when you point within 15° of the LZ you hear "On course". Default: 15°.

<a id="adv-settings-wind"></a>
## Settings: Wind

*Advanced Feature*

### Use manual wind (overrides estimate)
Tells the vario the wind instead of letting it guess.

Turn on if the windsock shows something different from the estimate. Default: Off.

### Wind from / Wind speed
The wind you enter when manual wind is on.

"Wind from" is the direction the wind comes from: a westerly is 270°. Wind speed is in your speed unit. Used only while *Use manual wind* is on.

<a id="adv-settings-gps"></a>
## Settings: GPS

*Advanced Feature*

### Update rate
How many times a second the GPS reports its position.

A rate of 5 times a second is smooth enough for guidance. Faster uses more power. Restart to apply. Default: 5 Hz.

### GPS module LED
The light on the GPS module.

On most modules it blinks once a second while there is a fix, which is handy on the bench and distracting in flight. Restart to apply. Default: Off.

### Ready to fly: minimum satellites
How many satellites the banner needs before it turns green.

For example, at 6, you wait for at least 6. A higher number waits longer but is more accurate. Default: 6.

### Ready to fly: maximum HDOP
How good the GPS accuracy must be before the banner turns green.

HDOP is lower for better accuracy. At 2.0 the banner waits for a good geometry. Default: 2.0.

<a id="adv-settings-bluetooth"></a>
## Settings: Bluetooth

*Advanced Feature*

### Send pseudo-GPS
Also sends position data to the app over Bluetooth.

FlyGaggle can then show the vario's GPS position and track. Turn it off if you only want vario data. Default: On.

The sentences are `$GPGGA` and `$GPRMC`. The altitude in them is the barometric altitude.

### Pseudo-GPS rate
How often the position is sent.

Once a second is enough for most apps. Default: 1 Hz.

<a id="adv-settings-logging"></a>
## Settings: Logging

*Advanced Feature*

### Record IGC flight logs
Records each flight to a file you can download.

From takeoff to landing the vario saves a track you can open in the usual flight-analysis sites, for example igcviewer.bgaladder.net. Download them on the Logs & system tab. Default: On.

The oldest logs are deleted automatically if the memory gets short.

### IGC pressure altitude referenced to GPS
Keeps the logged height from showing odd negative values.

Leave it on. Turn it off only if you specifically need the true standard-pressure altitude, which can be negative on a high-pressure day and makes some viewers complain. Default: On.

### Keep this much track before takeoff
Includes the last few seconds before takeoff in the log.

For example, at 30 s the file starts 30 seconds before takeoff, so the start of your run is not missing. It is kept in memory (not written to storage) until takeoff. 0 turns it off. Default: 30 s.

### Pilot name / Wing / motor
Text written into the header of each log.

Type your name and, for example, "Paramotor" or the wing and motor you fly. Default: blank and Paramotor.
