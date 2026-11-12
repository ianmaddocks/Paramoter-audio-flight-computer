# ParaVario user guide - Basic vario (no GPS module)

This guide is for the Basic ParaVario: a vario with no GPS module. It has a barometer, a speaker and a microphone, and it is a very good vario.

- It **beeps when you climb**, gives a **sink warning**, and **calls out your height** as you pass set heights.
- It notices **takeoff and landing by itself** and tells you your **flight time**.
- It sends vario data to **FlyGaggle** (or another app) over Bluetooth.
- Its **volume follows the noise around you** (the microphone), and it can read the **engine RPM** from the engine sound.
- It has a **settings page** you open on your phone.

It does **not** have a GPS module, so there is no speed, wind, landing zone, landing-approach or emergency guidance and no flight log. (A vario with a GPS module, the Advanced vario, adds all of those; see [User_Guide_(advanced).md](User_Guide_(advanced).md).)

There is only one program for both. The vario finds out at power-on whether a GPS module is fitted, and shows the Basic or the Advanced features to match: the page and its Help follow automatically. (If a vario that has a GPS is not being recognised, or you have taken one off, the setting **GPS module** in Settings, Advanced settings, GPS, can be set by hand.)

Every tile, button and setting is explained in [Feature_Reference.md](Feature_Reference.md), Part 1. This guide tells you how to use the vario; the reference tells you what each control does. Links below point to the entries you are most likely to want.

## The push button
There is one push button. On this version **any press** switches the vario beeps off or on. You hear "Vario muted" or "Vario on". Spoken calls (heights, flight time, battery) are never muted.


## Before you fly

1. **Switch on.** You hear a short climb beep, then a second later **"Ready"**. The beep is at your vario volume, so you can judge how loud the beeps will be.
2. **Wait a few seconds.** The barometer needs about five seconds to warm up and steady. If you open the web page, the banner at the top is amber and says `Vario settling`, and turns green and says `Ready to fly` when it is done. (Fly only when *you* are satisfied; the banner is a guide.)
3. **Stand still** where you will take off, with the vario in its normal flying position. The height it shows (`0 ft`) is measured from here.
4. **Take off.** You hear **"Take off"** once you have climbed about 50 feet (15 m). From then on the vario is flying.

Heights are measured from where you took off, not from sea level and not from the ground below you.

> **No beeps on the ground?** That is intended. With the default setting *Only when flying* the beeps start once takeoff is detected. To hear the beeps while testing the vario on the ground, turn that setting off, or just wait until you are airborne.

## What you will hear

**The vario**

- **Rising beeps** when you climb faster than about 0.3 m/s. Stronger lift gives a higher pitch and faster beeps.
- **A steady low tone** when you sink faster than about 2 m/s. The faster you sink, the lower it is.
- The beeps pause while the vario is speaking.

**Spoken calls**

| Call | When |
|---|---|
| "Take off" | You have taken off. |
| "250 feet" ... | You climb or descend through a set height. |
| "400 feet" | Every minute while flying: your height now. |
| "Flight time 1 hour 5 minutes" | Every 5 minutes. |
| "Landed, flight time ..." | You have landed. |
| "Battery low" | The battery is getting flat; repeats every 5 minutes. |
| "Vario muted" / "Vario on" | You pressed the button. |

All of these can be changed or switched off in Settings; see the *Height calls*, *Vario* and *Audio* entries in the [reference](Feature_Reference.md#basic-sounds).

## How takeoff and landing are noticed

With no GPS the vario uses the barometer alone.

- **Takeoff:** you have climbed about 50 feet (15 m) from where you stood. A slow rise, such as walking up a slope or carrying the vario upstairs, is not taken for a takeoff, because the vario follows slow changes in pressure while you are on the ground and only "locks" the ground level when it sees a real climb (faster than 0.5 m/s).
- **Landing:** the height has stayed steady for twice the *Landed after* time (60 seconds by default).

**Things to know**

- If you take off and **climb more slowly than 0.5 m/s** at first, the ground level keeps following you up. A normal climb-out is faster than that. If you often climb out slowly, lower the setting *Ground level held when climbing or sinking faster than*.
- Flying **perfectly level for a minute or more** can look like a landing ("Landed, flight time ..." in mid-air). If that happens, increase *Landed after*. After a false landing the vario needs another 50 foot climb before it notices you are flying again.
- If you **switch the vario on in the air**, it treats where you are as the ground until you climb 50 feet; heights will not be right. Switch it on at the launch.

## The web page

1. On your phone, join the Wi-Fi network **ParaVario-XXXX** (the default password is **paramotor**). XXXX is a code unique to your vario, so you can tell several varios apart; the same code is in its Bluetooth name.
2. The page opens by itself. If it does not, open **http://192.168.4.1** in a browser.

The phone has no internet while joined to the vario, so turn mobile data on if you need both.

There are four tabs:

| Tab | What is there |
|---|---|
| **Flight** | The ready banner; tiles for **Height**, a **height graph** of the last 10 minutes, **Vario**, **Flight time**, **Battery**, **Engine RPM** and **Background noise**; and buttons for **Mute vario**, **Test voice** and **Full screen** (hides the browser's bars). Refreshed every second. |
| **Settings** | Every setting, in groups. Change a value and it is saved a moment later. A setting marked *restart* takes effect after a restart. The rarely needed groups (Battery, Wi-Fi, Engine RPM and the GPS module choice on this version) are inside **Advanced settings** at the end. |
| **Logs & system** | The **Logging** section (live messages from the vario, for finding problems), firmware update, **Restart** and **Factory defaults**. (The flight-log list is empty on this version, because logs need a GPS.) |
| **Help** | Opens this guide and the feature reference on GitHub (the latest version; the phone needs internet, so turn mobile data on). Each group on the Settings tab also has a **Help** link to its entry in the reference. |

Heights follow your chosen unit (feet or metres), and the heights you type in settings are in that unit too.

**Using the page like an app.** On Android, tap **Full screen** to hide Chrome's bars. On an iPhone, open the page in Safari, tap Share, then **Add to Home Screen**, and open the vario from the new icon: it opens without Safari's bars.

Details of each tile and button: [reference, Tiles](Feature_Reference.md#basic-tiles), [Buttons](Feature_Reference.md#basic-buttons).

## Common things to set

| I want to... | Setting | Where it is explained |
|---|---|---|
| Use metres instead of feet | Height (Units) | [Units and Audio](Feature_Reference.md#basic-settings-units-and-audio) |
| Make the beeps louder or quieter | Vario volume | [Units and Audio](Feature_Reference.md#basic-settings-units-and-audio) |
| Make the voice louder or quieter | Speech volume | [Units and Audio](Feature_Reference.md#basic-settings-units-and-audio) |
| Hear beeps for weaker or stronger lift only | Climb threshold | [Vario](Feature_Reference.md#basic-settings-vario) |
| Change the pitch of the beeps | Base tone, Tone rise per m/s | [Vario](Feature_Reference.md#basic-settings-vario) |
| Change when heights are spoken | Boundaries when climbing / descending | [Height calls](Feature_Reference.md#basic-settings-height-calls) |
| Stop the height every minute | Periodic height every = 0 | [Height calls](Feature_Reference.md#basic-settings-height-calls) |
| Detect takeoff sooner or later | Takeoff height gain without GPS | [Takeoff and landing](Feature_Reference.md#basic-settings-takeoff-and-landing) |
| Stop a false "Landed" | Landed after (increase) | [Takeoff and landing](Feature_Reference.md#basic-settings-takeoff-and-landing) |
| Name my vario in the app | Device name | [Bluetooth](Feature_Reference.md#basic-settings-bluetooth) |
| Reach the vario from my home computer | Server network name and password | [Wi-Fi](Feature_Reference.md#basic-settings-wi-fi) |

## Bluetooth (FlyGaggle)

The vario shows up as **ParaVario-XXXX**. In FlyGaggle, add a Bluetooth vario and pick it. The vario sends your climb rate, altitude, temperature, pressure and battery (`$LK8EX1`, `$LXWP0`). Because there is no GPS, it sends **no position**: your phone's own GPS provides that.

## Battery

The Battery tile shows the percentage, the voltage, and an estimate of the time left (for example `~3 h 20 min`). The estimate is rough at first. After you have once run the vario until the battery is flat and then charged it, the vario learns your battery and the estimate becomes accurate. Details: [Battery settings](Feature_Reference.md#basic-settings-battery).

## Maintenance

- **First install (USB cable):** from the project folder, `pio run -e release_usb -t upload`.
- **Updates over Wi-Fi from the computer:** join the vario's Wi-Fi (or, if you have set the *Server network*, just power the vario on near your computer), then `pio run -e release -t upload`. The vario restarts when it is done.
- **Updates from the phone:** build with `pio run -e release`, copy `.pio/build/release/firmware.bin` to the phone, join the vario's Wi-Fi, open **http://192.168.4.1** in the normal browser, go to the **Logs & system** tab, choose the file, enter the update password and tap **Install update**. Keep the phone close and the page open until it says *Installed*. A failed or interrupted upload leaves the old program running. Updates are refused while the vario is flying.
- **One program for every vario.** The same `firmware.bin` goes on a vario with and without a GPS module.
- **Voice clips:** edit `tools/vocab.txt`, run `python tools/make_voice.py`, then `pio run -e release -t uploadfs`. This erases stored flight logs (there are none on this version). Settings are kept.
- **Factory defaults:** web page, Logs & system tab, *Factory defaults*.

## Troubleshooting

| What happens | Likely reason |
|---|---|
| No beeps on the ground | *Only when flying* is on and takeoff has not been detected yet. |
| No beeps in flight | The beeps are muted (the Vario tile says `muted`; press the button), or *Vario beeps enabled* / *Climb beeps* is off, or the volume is 0. |
| Banner says `Vario settling` for a long time | The vario is being moved or is in a draught. Keep it still for a few seconds. |
| Banner says `Barometer not working` | The barometer is not detected; check its wiring. |
| "Take off" never comes | You have not climbed 50 feet yet, or the climb was slower than *Ground level held ...* (default 0.5 m/s). |
| A false "Landed" in flight | Very level flying; increase *Landed after*. |
| Cannot find the vario's Wi-Fi | Look for `ParaVario-` followed by four characters, not just `ParaVario`. |
