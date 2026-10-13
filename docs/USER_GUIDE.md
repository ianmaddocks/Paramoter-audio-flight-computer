# ParaVario user guide

ParaVario is an audio vario and flight computer for paramotor flying. It beeps when you climb, calls out your height above the landing zone (LZ), talks you around a landing circuit, and guides you back to the LZ if the engine stops. It sends vario and GPS data to FlyGaggle over Bluetooth and has a settings page you open on your phone.

## Before you fly

1. Switch on. You hear **"Vario ready"**.
2. Wait outside for **"GPS fix"**.
3. Optionally, open the web page on your phone (see [The web page](#the-web-page)). The banner at the top turns green and reads **Ready to fly** once the GPS has enough satellites, the barometer works and the landing zone is known. It shows the satellite count against the minimum, for example `7 / 6 satellites`. The banner is only a guide; whether to fly is your call.
4. Take off. Takeoff is detected automatically and you hear **"Take off"**. The spot where you stood before the takeoff run becomes the landing zone, and its height becomes zero for all height calls. To use a different LZ, see [Landing zone](#landing-zone).

## Button

| Press | What it does |
|---|---|
| **Single press** | Starts **landing approach** guidance ("Landing approach…"). Press again to cancel ("Approach cancelled"). Does nothing while emergency mode is on. |
| **Double press** | Starts **emergency glide** guidance ("Emergency, glide range…"). Double press again to cancel ("Emergency cancelled"). Starting emergency mode cancels any landing approach. |
| **Long press** (hold about 1 s) | Mutes or unmutes the **vario beeps** ("Vario muted" / "Vario on"). Spoken calls are never muted. |

A single press takes effect about half a second after you let go, because the vario waits to see whether a second press follows.

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
| "Flight resumed" | The vario restarted in flight and carried on with the same LZ and flight time. |

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

1. On your phone, join the Wi-Fi network **ParaVario** (default password **paramotor**).
2. The page usually opens by itself. If it doesn't, open **http://192.168.4.1** in a browser.

The phone has no internet while it's on this network, so turn mobile data on if you need both.

| Section | Contents |
|---|---|
| Ready banner | Green **Ready to fly**, or amber with what's missing and the satellite count. Hidden while flying. |
| Tiles | Height above the LZ (and above sea level), vario, ground speed and track, flight state and time, mode, LZ distance and bearing, wind, GPS satellites and HDOP, battery. Refreshed every second. |
| Actions | Landing approach, Emergency glide, Mute vario (same as the button), **Test voice**, **Set LZ here**, **LZ = takeoff**. |
| Settings | Collapsible groups (see below). Change values, then tap **Save settings** at the bottom. Settings marked *restart* apply after a restart, which the page offers. |
| Flight logs | IGC files for each flight. Tap a name to download or Delete to remove. The oldest logs are deleted automatically when space runs low. |
| Debug log | Live log lines from the vario, with Pause, Clear and Save as file. Opens automatically on the debug firmware. The same lines appear on the USB serial port. |
| System | Restart, Factory defaults. |

Heights, distances and speeds on the page follow your chosen units, and you always enter them in those units.

## Configuration options

The defaults below are shown in the default units (feet, metres, km/h).

### Units
| Setting | Default | Notes |
|---|---|---|
| Height | Feet | Feet or metres, for speech and the page. |
| Distance | Metres / km | Metres (km above 1000 m) or feet (miles above ½ mile). |
| Speed | km/h | km/h, mph or knots (page only). |

### Audio
| Setting | Default | Notes |
|---|---|---|
| Speech volume | 90 % | |
| Vario volume | 60 % | |

### Vario
| Setting | Default | Notes |
|---|---|---|
| Vario beeps enabled | On | |
| Only when flying | On | Silent on the ground. |
| Climb beeps | On | |
| Sink alarm | On | |
| Climb threshold | 0.3 m/s | Beeps start above this. |
| Sink alarm threshold | −2.0 m/s | Tone starts below this. |
| Base tone | 700 Hz | Pitch at zero climb. |
| Tone rise per m/s | 100 Hz | |
| Responsiveness | 1.0 | Higher reacts faster but is noisier; lower is smoother. |

### Height calls
| Setting | Default | Notes |
|---|---|---|
| Announce height boundaries | On | |
| Boundaries | 50, 100, 150, 250, 350, 500, 750, 1000 ft | Comma-separated, in your height unit. |
| Repeat every (above last boundary) | 500 ft | 0 = no calls above the last boundary. |
| Hysteresis | 10 ft | Stops repeat calls while hovering near a boundary. |
| Announce when climbing / descending | On / On | |
| Periodic height every | 60 s | 0 = off. Resets after a boundary call. |
| Flight time every | 5 min | 0 = off. |

### Takeoff & landing
| Setting | Default | Notes |
|---|---|---|
| Takeoff ground speed | 15 km/h | |
| Takeoff speed held for | 5 s | |
| Takeoff height gain | 33 ft | Needed as well as the speed. |
| Landed below ground speed | 5 km/h | |
| Landed after | 30 s | |
| Landed below height | 98 ft | |

### Glide
| Setting | Default | Notes |
|---|---|---|
| Glide ratio (engine off) | 5.0 :1 | Used for emergency range and the approach. |
| Trim airspeed (engine off) | 38 km/h | Replaced by the measured value once wind has been estimated from turns. |

### Landing zone
| Setting | Default | Notes |
|---|---|---|
| Landing zone | Takeoff point | **Takeoff point**: captured automatically before each takeoff. **Manual**: uses the values below. |
| Latitude / Longitude | – | Manual LZ. Easiest to set by standing on it and tapping **Set LZ here**. |
| Elevation (MSL) | – | Manual LZ height above sea level. **Set LZ here** fills it in. |

The LZ, ground height and flight state are saved to the vario's memory, so a restart in flight carries on where it left off.

### Landing approach
| Setting | Default | Notes |
|---|---|---|
| Circuit direction | Pilot's choice | Pilot's choice, Left hand or Right hand. |
| Circuit height | 400 ft | "Descend to" is called if you're well above this. |
| Upwind leg | 400 m | |
| Crosswind leg | 400 m | Also the base leg length. |
| Downwind leg (target) | 400 m | Extended automatically if you're high. |
| Start final at | 164 ft | Target height at the start of final (50 m). |
| Minimum final length | 100 m | |
| Circuit start capture radius | 80 m | How close counts as "arrived". |
| Call turns early by | 3 s | Allows for reaction time and turn radius. |
| Off-heading warning | 30 ° | Also the emergency off-course limit. |
| Guidance update every | 20 s | Distance/clock updates on the way to circuit start. |

### Emergency
| Setting | Default | Notes |
|---|---|---|
| Distance update every | 30 s | |
| On-course tolerance | 15 ° | |

### Wind
| Setting | Default | Notes |
|---|---|---|
| Use manual wind | Off | When on, overrides the estimate (e.g. from the windsock). |
| Wind from | 0 ° | Direction the wind blows from. |
| Wind speed | 0 km/h | |

### GPS
| Setting | Default | Notes |
|---|---|---|
| Update rate | 5 Hz | *Restart.* |
| Ready to fly: minimum satellites | 6 | Used by the ready banner. |
| Ready to fly: maximum HDOP | 2.0 | Lower HDOP means better accuracy. |

### Bluetooth (FlyGaggle)
| Setting | Default | Notes |
|---|---|---|
| Device name | ParaVario | *Restart.* Name shown when pairing. |
| Vario sentences rate | 5 Hz | `$LK8EX1` and `$LXWP0`. |
| Send pseudo-GPS | On | `$GPGGA` / `$GPRMC`, with barometric altitude. |
| Pseudo-GPS rate | 1 Hz | |

### Wi-Fi
| Setting | Default | Notes |
|---|---|---|
| Network name | ParaVario | *Restart.* |
| Password | paramotor | *Restart.* At least 8 characters; blank = open network. |

### Battery
| Setting | Default | Notes |
|---|---|---|
| Divider ratio | 2.0 | Matches the 51k/51k divider on D1. |
| Calibration factor | 1.0 | Adjust if the page disagrees with a voltmeter. |
| Low battery warning | 3.5 V | |

### Logging
| Setting | Default | Notes |
|---|---|---|
| Record IGC flight logs | On | |
| Pilot name | – | Written into each IGC file. |
| Wing / motor | Paramotor | Written into each IGC file. |

## Maintenance

- **Firmware:** `pio run -e release -t upload`. Use `-e debug` for detailed logging on USB and the web page.
- **Voice clips:** edit `tools/vocab.txt` or add your own recordings to `tools/voice_custom/`, run `python tools/make_voice.py`, then `pio run -e release -t uploadfs`. **This erases the flight logs stored on the vario, so download them first.** Settings are kept.
- **Factory defaults:** web page → System → Factory defaults.
