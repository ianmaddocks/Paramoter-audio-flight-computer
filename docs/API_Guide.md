# ParaVario API guide

For people writing software that talks to the vario (a phone app, a PC tool, a home server). Pilots do not need this: see the
[user guides](../README.md).

The vario has a small HTTP server on port 80, on its own Wi-Fi (the access point, address 192.168.4.1) and on the server network it
joins if one is set. It is also found as `paravario.local`. Replies are JSON unless said otherwise. Errors are `{"error":"..."}` with
a 4xx or 5xx status. Send a request body with the header `Content-Type: application/json`; without it the vario does not read the body as JSON.

The vario is a small device with little memory. Make one request at a time, do not poll faster than about once a second, and close
the connection when done. Many parallel connections make it hold requests back, and a page left open slows it down.

**There is no login.** Anyone on the vario's network can change settings and areas, start and cancel guidance, restart it, and read the
flight logs. Only the firmware update (`/api/ota`) needs a password. Keep it on a network you trust.

## Reading

| Request | Gives |
|---|---|
| `GET /api/status` | Everything live, as one JSON object (below). |
| `GET /api/settings` | Every setting and its value, as one JSON object `{key: value}`. |
| `GET /api/schema` | The settings table: for each setting its key, group, label, unit, limits and choices. The settings page is built from it, so a setting added in the firmware appears with no change in your client. Settings for hardware that is not fitted are left out. |
| `GET /api/areas` | The stored areas (below). |
| `GET /api/logs` | The flight logs: `{"files":[{"n":"name.igc","s":bytes,"p":pieces}],"free":bytes,"total":bytes}`. |
| `GET /api/log?name=<name>.igc` | One flight log, as an IGC file download. |
| `GET /api/height` | The last height trace: binary, little endian. `u16 count`, `u16 seconds per sample`, then `count` signed 16-bit heights (metres). |
| `GET /api/spectrum?since=<n>` | Microphone spectrum (only where the build has it): binary; 204 if nothing newer than `n`. |
| `GET /api/debuglog?since=<n>` | The debug log as text. First line `#next=<n> dropped=<n> debug=<0\|1>`, then one line per event. Ask again with `since` set to `next` to get only what is new. |
| `GET /api/nettrail` | Text: the last 48 network events, kept across restarts. |
| `GET /api/coredump` | The chip's record of its last crash (binary); `?erase=1` deletes it. 404 if none. |

### `/api/status`

The main groups (a group is left out when the hardware is not fitted or there is nothing to say):

| Key | Contents |
|---|---|
| `ready`, `notReady`, `minSats` | Ready to fly, or the reason it is not. |
| `uptime`, `resetReason`, `restartNeeded`, `version`, `git`, `release` | Firmware and last restart. `restartNeeded` is true after a setting that only takes effect on restart. |
| `heap`, `heapMin`, `heapBlock` | Free memory now, lowest since power-up, largest free block (bytes). |
| `flying`, `flights`, `flightTime`, `agl`, `altMsl`, `vario`, `muted` | Flight state, height above ground and above sea level (m), vertical speed (m/s). |
| `hw` | Which hardware is fitted: `gps`, `mic`, `spectrum`. |
| `gps` | `link`, `fix`, `sats`, `hdop`, `lat`, `lon`, `speed` (m/s), `moving`, `track` (deg), `alt`. |
| `baro` | `ok`, `pressure`, `temp`, `rej`, `jumps`, `frozen`. |
| `batt` | `v`, `pct`, `min`, `learned`. |
| `mic` | `ok`, `noise`, `vol`, `rpm`, `raw`, `conf`. |
| `wind` | `valid`, `from` (deg), `speed` (m/s), `src`. |
| `lz` | The landing zone: `valid`, `lat`, `lon`, `elev`, `dist`, `brg`. |
| `area` | Area warnings: `state`, `edge`, `rstate`, `rdist`. |
| `glide`, `mode`, `approach`, `range`, `vol` | Glide ratio, guidance mode (`none`, approach, emergency), volumes being applied. |
| `wifi`, `ble`, `logging` | Network and Bluetooth state, and the flight log being written. |

All distances are metres, speeds m/s and angles degrees, whatever units the pilot chose for the display.

## Changing

| Request | Does |
|---|---|
| `POST /api/settings` | Body: a JSON object with only the settings to change, `{"speechVolume":30,"pilotName":"Ian"}`. Keys and limits are in `/api/schema`. Reply: `{"ok":true,"restart":false,"settings":{...}}`; `restart` is true if one of them needs a restart to take effect. |
| `POST /api/areas` | Replaces all areas. See below. 400 with the reason if not usable (nothing changes). |
| `POST /api/action?do=<action>` | One of the actions below. Reply `{"ok":true}`. |
| `DELETE /api/log?name=<name>.igc` | Deletes one flight log. |
| `POST /api/ota` | Firmware update: multipart form with the image, header `X-OTA-Password`. `?target=fs` updates the file system image instead; `?md5=<hex>` checks it. The vario restarts when it succeeds. 401 for a wrong password. |

Actions (`do=`):

| Action | Does |
|---|---|
| `approach` | Starts or cancels landing guidance. |
| `emergency` | Starts or cancels emergency guidance. |
| `mute` | Mutes or unmutes the vario sound. |
| `tell_me` | Speaks the status (the single click of the button). |
| `reset_flight` | Forgets the flight state (use if it wrongly thinks it is flying). |
| `lz_here` | Sets the landing zone to the present position (409 without a GPS fix). |
| `lz_takeoff` | The landing zone is where the takeoff was. |
| `test_voice` | Speaks a test announcement. |
| `defaults` | Every setting back to its default, then restarts. |
| `restart` | Restarts. |

## Areas

```json
{"a":[{"n":"Field","on":true,"k":"safe","p":[[51.7972,0.9197],[51.8008,0.9292],[51.7975,0.9315]]},
      {"n":"Range","on":true,"k":"restricted","w":500,"p":[[51.8186,0.9296],[51.8162,0.9410],[51.8195,0.9420]]}]}
```

- `n` name, `on` enabled, `k` is `"safe"` (stay inside) or `"restricted"` (keep out), `w` the warning distance in metres for a
  restricted area, `p` the corners as `[latitude, longitude]` in degrees, in order round the edge.
- Up to 25 areas of 3 to 24 corners each. An area must be at least about 10 m across and no more than 100 km across.
- A POST replaces everything: send all the areas you want to keep.
- The body may be at most 5 kB (about 180 corners in all). The vario needs several times the body size in free memory while it handles
  an upload (a 7 kB one took its free memory down to 7 kB), and a much larger one can crash it and make it restart. Over 5 kB it
  answers 400 `too much data`, but only if it survived reading it, so keep to the limit.
- The areas are stored in flash and survive restarts.

## Test build only

A separate test firmware (see [TESTING.md](TESTING.md)) adds `GET /api/sim?key=value&...`, which replaces the sensors with simulated
ones and runs the on-device unit suites. It is not in the release firmware and is not for other uses.
