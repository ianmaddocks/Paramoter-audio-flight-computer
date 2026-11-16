# ParaVario sensor API, version 1 (DRAFT for review)

This project is the **vario**. It **defines the API in both directions** and implements the vario's side:

- the **vario's endpoints**, which a sensor calls to pair, register, send heartbeats and push live telemetry (sections 4, 5); and
- the **sensor-side endpoints** (section 9), which every sensor implementation exposes and the **vario calls** to read a sensor's history,
  acknowledge it and read its status and configuration (section 6).

**What a sensor is built from, how it measures, stores or updates itself is not this project's concern**: it is neither specified nor assumed
beyond these endpoints. A sensor project implements the sensor-side endpoints exactly as defined here.

Status: **draft, not implemented, not tested on any hardware.** Nothing here may be called stable until the tests `HUB-001..035`
(brief section 13) pass against the vario implementation.

Companion files: `openapi.yaml` (the endpoints and schemas, machine-readable), `examples/` (messages), `diagrams.md` (sequence and
state diagrams), `reference/pv_crypto.py` (message-authentication code with test vectors).

Words: **vario** = the ParaVario flight computer. **client** or **sensor** = a device that implements this API.
MUST, SHOULD, MAY as in RFC 2119.

## 1. Model

A **device** (`device_id`) has one or more **channels** (`ch` 0..7). A channel measures one **type** (`egt`, `cht`; further types are
added by name without changing the protocol) in one **unit**. A device with two cylinder-head sensors is one device with two
channels. One device is not one channel.

A **record** is one measurement: `[seq, up_ms, ch, value, st]`. A sensor sends **live telemetry** to the vario (the current state, not kept by the
vario). A sensor also keeps its records durably, and the **vario pulls the history** from the sensor in bounded batches and **acknowledges** it, after
which the sensor may discard it (hybrid model: live push, history pull).

| Item | Limit |
|---|---|
| Channels per device | 8 |
| Records in one telemetry request | 16 |
| Records in one history response | 32 |
| Request or response body | 1024 bytes (a history response: 2048) |
| Identifier characters | `[0-9a-f]` lowercase for ids, nonces, keys, macs |

## 2. Transport

HTTP/1.1, plain TCP (no TLS, see the security limits in section 7), JSON bodies (`Content-Type: application/json`), one request per
connection (`Connection: close`). Telemetry uses integers only: fixed-point with the channel's `scale`, real value = `value / scale`.

The vario is a Wi-Fi access point, by default at `192.168.4.1`, and answers on port 80. A client joins that access point as a station
and finds the vario at its **default gateway**; it MUST NOT hard-code an address.
A sensor serves its own endpoints (section 9) on port 80 at the address the vario's DHCP gave it (or on `api_port` from `register`); the vario
learns the address from the source address of the sensor's `register` request. The same timeouts, error codes and `Connection: close` apply in both
directions, and a sensor MUST keep its measuring and storing independent of how busy its web server is.

A client SHOULD use a 3 s connect and 5 s response timeout and MUST NOT let a slow or absent vario stop it doing its own job.
Retry: exponential backoff 1, 2, 4, 8, 16, 30 s (then every 30 s), up to 25 % random jitter, and keep what it has not been able to
send. Losing the link MUST NOT make a client forget its pairing.

Errors are `{"err":"<code>","msg":"<text>"}` with an HTTP status:

| Status | `err` | Meaning | Client action |
|---|---|---|---|
| 400 | `bad_request` | malformed JSON or field | fix; do not retry unchanged |
| 401 | `auth` | missing or invalid MAC, or a counter that did not increase | register again once; if it persists, treat as unpaired |
| 403 | `not_paired` | the vario has no pairing for this device, or pairing is not open | stop; wait for the owner |
| 404 | `unknown` | unknown path or id | stop |
| 409 | `conflict` | the session or `boot` does not match | register again |
| 413 | `too_large` | body over the limit | send fewer records |
| 422 | `invalid` | well-formed but not acceptable (unknown channel, bad value) | drop those records, continue |
| 426 | `version` | no common protocol version; body has `supported:{min,max}` | stop |
| 429 | `busy` | too many requests; `Retry-After` header, seconds | wait that long |
| 503 | `busy` | the vario is short of resources; `Retry-After` | wait that long, keep what it has |

## 3. Identity and discovery

- `device_id`: 12 lowercase hex characters, unique per device (the client's MAC address without separators is the usual choice).
- `vario_id`: 12 lowercase hex characters, the vario's Wi-Fi MAC address.
- `GET /api/v1/system/identity` (no authentication, callable before pairing):
  `{"vario_id","name","proto":{"min":1,"max":1},"pairing_open":false,"fw":"v1.1"}`.

The vario's SSID is the owner's setting (default `ParaVario-` plus the last four hex digits of its MAC, but it can be renamed), so a
client MUST NOT depend on the SSID pattern. A client that scans for networks should offer the owner those that answer `identity`
with a protocol range containing 1, and **show `vario_id` and `name`** so the owner confirms the right vario. SSID alone is never
authentication.

## 4. Pairing and authentication

**One keyed hash does both jobs: it proves who sent a request and that it was not altered.** The key is a random **pairing secret** that is
known to both sides and **shared out of band** (typed in by the owner), never sent over the air.

**Pairing.** The owner starts pairing on the vario (its web page, Phase 3). The vario generates a random **64-bit pairing secret**, shows it as
**16 lowercase hex characters** on the page (it is long on purpose, see section 7), and accepts pairing for **120 s and 3 failed attempts in
total**, one client at a time, then closes pairing mode. The owner gives the secret to the client (typing it in). The client then calls

`POST /api/v1/pair` `{"device_id","type","proto":{"min","max"},"nonce_s"}` with the `X-PV-Auth` header (below; session `00000000`, counter 0).

The vario recomputes the MAC with the secret it is showing. If it matches, the vario binds `device_id` to that secret in persistent storage (with
`type`) and replies `{"vario_id","name","proto":1,"mac"}`, where `mac` = first 8 bytes (16 hex) of
`HMAC-SHA256(secret, "PVP1-pair|nonce_s|device_id|vario_id")`. **The client checks `mac`**: a vario that was not shown the same secret cannot produce
it, so a client that was given the secret of one vario but joined another finds out at once (test HUB-008). A wrong MAC is a failed attempt (401);
after 3 the vario closes pairing mode. If pairing is not open the answer is 403. `nonce_s` is 16 random bytes (32 hex) chosen by the client for
each attempt. Each time pairing is opened the vario makes a **new** secret.

After pairing the vario keeps the secret for that device and the client keeps the secret and the `vario_id`.

**Request authentication.** Every request after `pair`, **in either direction**, carries `X-PV-Auth: <sender_id>:<session>:<counter>:<mac>`, the sender id being the
`device_id` for requests to the vario and the `vario_id` for requests to a sensor. `mac` = first 8 bytes (16 hex) of
`HMAC-SHA256(secret, METHOD LF path LF session LF counter LF body)`, where `path` includes the query string, `body` is empty for a GET, and LF is a
single line feed. `session` is the 8-hex session id from `register` (`00000000` for `pair` and `register` themselves). The counter starts at 0 for
`register` and at 1 for the first request of a session; **each direction has its own counter**, and the receiver (the vario for a sensor's requests, the
sensor for the vario's) rejects (401) a request whose counter is not greater than the last it accepted in that session. A retry after a failed attempt uses a higher counter; duplicate *data* is caught by sequence numbers (section 6). A request replayed
from an earlier session fails because the vario issues a new random session id each time (the sensor learns it from the `register` response). Responses
are not signed, except the `mac` of the `pair` and `register` responses.

## 5. Registration, heartbeat, live telemetry, time

**`POST /api/v1/sensors/register`** (authenticated, session `00000000`, counter 0). Request: `device_id`, `type`, `fw`, `hw`, `proto:{min,max}`, `boot` (8 hex,
random for each start-up; the base of `up_ms`), `epoch` (8 hex, identifies the client's record sequence, section 6), `nonce_s`, `batt`
(true if it reports a battery), `ch`: channels `{ "ch":0, "type":"egt", "unit":"degC", "scale":10, "min":-50, "max":1100, "name":"EGT" }`
(`min`/`max`: the measuring range in real units, used by the vario to flag over/under-range), optional `cal` text, optional `api_port` (default 80: where the
sensor serves its endpoints).
Response: `proto` (highest common version), `session` (8 hex, random, new for each register), `telemetry_s` (the interval the vario would like; a longer one up to
`stale_s` is allowed), `heartbeat_s` (default 10), `stale_s` (default 10), `max_batch` (<=16), `acked` (the highest sequence the vario has
durably stored and acknowledged for this `epoch`, 0 when new), `mac` (the vario's proof: first 8 bytes (16 hex) of `HMAC(secret, "PVP1-reg|nonce_s|session")`),
`time`. 426 for no common version, 403 if not paired, 422 for an invalid channel list; a repeat with identical content is idempotent.

**`POST /api/v1/sensors/heartbeat`** (authenticated): `{ "boot", "up":<ms>, "batt":<0-100 or null>, "rssi":<dBm>, "last":<newest sequence>, "st":<0 ok, else a device fault code> }`,
sent every `heartbeat_s` when no telemetry was sent (telemetry counts as a heartbeat). `last` tells the vario whether the sensor holds records above `acked`,
which is when it pulls. Response `{ "ok":true, "time":{...} }`.

**`POST /api/v1/sensors/telemetry`** (authenticated):

```json
{"boot":"0a1b2c3d","up":123456,"rec":[[1041,5000,0,6153,0],[1042,6000,0,6160,0]]}
```
`up` is the client's uptime in ms at sending; `rec` the records `[seq, up_ms, ch, value, st]`:

| Field | Mandatory | Meaning |
|---|---|---|
| `seq` | yes | unsigned 32-bit, +1 per record within an `epoch` |
| `up_ms` | yes | the client's uptime at the measurement (monotonic, ms), same base as `up` |
| `ch` | yes | channel index from `register` |
| `value` | yes (`null` when `st` is not 0) | integer; real value = `value / scale` |
| `st` | yes | 0 ok; 1 open circuit; 2 short circuit; 3 over range; 4 under range; 5 frozen reading; 6 implausible jump; 7 not ready; 8 calibration needed; 9 internal fault |

An optional sixth element `utc` (unsigned seconds) only if the client has been given valid time. The vario validates `ch`, `st` and the
range of `value`, keeps the newest valid value per channel and replies
`{"rx":<highest seq accepted>, "dup":<ignored>, "gap":<missing before this batch>, "time":{...}}`.
Telemetry is **not stored and not acknowledged as durable**; it does not let a client delete anything.

**Time: `GET /api/v1/system/time`** (no authentication; also in responses as `time`):
`{ "utc":1763241540, "valid":true, "src":"gps", "unc_ms":1000, "vario_up":8421551 }`. `valid:false`, `src:"none"` while the vario has no valid GPS UTC;
a client MUST then NOT invent a UTC for its records. The client keeps `seq` and `up_ms` independent of UTC. **Untimed records:** at every
request where UTC is valid the vario stores an anchor `(device, boot, client up, vario UTC)`; a record `(boot, up_ms)` gets
`utc = anchor.utc + (up_ms - anchor.up)/1000` from the nearest anchor of the same `boot` (error: round trip plus `unc_ms`, usually under 2 s,
plus client clock drift). With no anchor for that `boot` the record stays untimed and is never guessed.

## 6. Historical records: pulled by the vario, acknowledged, idempotent

A record is identified by `(device_id, epoch, seq)`, never by time. The sensor keeps every record in durable local storage under an `epoch`: 8 hex
characters chosen at random when its record store is created or erased (or at every start-up if it cannot keep a counter); within an epoch `seq` increases
by 1 and survives restarts.

**Pull, by the vario** (a vario-to-sensor request, section 9): `GET /api/v1/records?epoch=<e>&after=<seq>&max=<n>` (authenticated). Response
`{"epoch":"1a2b3c4d","boot":"0a1b2c3d","first":1041,"last":1072,"more":true,"rec":[[...],...]}`: records `[seq, up_ms, ch, value, st]` in ascending
`seq` (a record from an earlier start-up carries its `boot` as a trailing element when it differs from the response's `boot`); `more` says whether
records after `last` exist; `lost:<n>` is added when records inside the range were overwritten on the sensor. With no `after` the sensor starts at its
oldest stored record. The vario asks for at most `max_batch`-sized batches of up to 32 records.

**Acknowledge, by the vario:** after validating the batch and writing it to persistent storage the vario calls `POST /api/v1/records/ack`
`{"epoch":"1a2b3c4d","upto":1072}` on the sensor, which answers `{"ok":true,"free":<records of space left>}`. The sensor deletes or marks records up to and
including `upto` **only after receiving this**, accepts the same acknowledgement again (idempotent) and ignores one lower than it already holds. A sensor MUST
NOT delete records because the vario was merely reachable or because live telemetry was accepted.

**When the vario pulls.** At `register` (the response gives `acked`) and whenever a heartbeat or telemetry shows `last` above `acked`, **from a
dedicated low-priority task of the vario, never from its flight loop**, one bounded batch at a time with a pause between batches. History never takes
priority over live telemetry and is paused while the vario is short of memory or flying with little free heap.

Behaviour (these are tests `HUB-018..024`):
- *Duplicate or repeated batch:* the vario already holds `(epoch, seq)` and ignores it; the same batch twice makes one set of data.
- *Out of order:* a record below `acked` is a duplicate; one inside a batch out of order is sorted.
- *Missing sequence numbers:* the vario notes the gap and the pull repairs it unless the sensor reports `lost`.
- *Lost acknowledgement:* the sensor still holds the records; the vario asks `after=<its position>` again, receives them again, ignores the duplicates and
  acknowledges again.
- *Interrupted transfer:* nothing is acknowledged until stored, so the next pull restarts from the same `after`.
- *Vario restart during a transfer:* the vario keeps `(device, epoch) -> acked` in persistent storage, written after each stored batch and before the
  acknowledgement is sent.
- *Sensor restart during a transfer:* the same `epoch` continues (the sensor's counter is persistent); a new `epoch` is a new stream from `seq` 1 and the
  vario keeps its position for the old epoch.
- *Storage exhausted on the sensor:* the oldest unacknowledged records are overwritten and the next batch reports `lost`.
- *Large backlog:* pulled one batch at a time at the vario's pace; the vario never holds more than one batch in memory.

Test vectors (computed by `reference/pv_crypto.py`; an implementation MUST reproduce them):

```
secret          7f3a9c2e5b1d4860
vario_id        ac276e8112b8
device_id       5ccf7f0a1b2c
nonce_s         00112233445566778899aabbccddeeff
session         c0ffee11
pair body       {"device_id":"5ccf7f0a1b2c","type":"egt","proto":{"min":1,"max":1},"nonce_s":"00112233445566778899aabbccddeeff"}
X-PV-Auth       5ccf7f0a1b2c:00000000:0:8115c1438e44baf7        (POST /api/v1/pair, session 00000000, counter 0)
pair reply mac  9d70855c7be5656e
register reply mac  f4ae9e995526aab3
telemetry body  {"boot":"0a1b2c3d","rec":[[1041,5000,0,6153,0]]}
X-PV-Auth       5ccf7f0a1b2c:c0ffee11:1:de61f8c8be3b22d0        (POST /api/v1/sensors/telemetry, session c0ffee11, counter 1)
X-PV-Auth       ac276e8112b8:c0ffee11:1:478aa40a4958ea0f        (vario to sensor: GET /api/v1/records?epoch=1a2b3c4d&after=1040&max=32, session c0ffee11, vario counter 1)
```

## 7. Security assumptions and limits (stated, not hidden)

- Aim: stop **accidental** cross-pairing (several varios and clients in range), stop **casual** injection or alteration of readings, and keep the
  secret off the air. Not aimed at a determined attacker who can jam, has the Wi-Fi password and records traffic for a long time.
- The secret is used directly as the HMAC key, so anyone who records a message could try secrets offline. That is why it is **64 bits of
  randomness (16 hex characters), not a short code**: 2^64 trials is not practical. A 6-digit code would not be safe here. The 8-byte tag
  is likewise enough against online forgery given the replay counter.
- Online guessing is limited: pairing is open 120 s and for 3 failed attempts, one client at a time. Without a pairing window, `pair` answers 403.
- Replay: a captured request is useless in another session (the vario issues a new random session id each time) and within a session the counter must
  increase. Reordering within a session is rejected the same way, in either direction.
- The content is **not encrypted** (temperatures are not secret); this is integrity and origin, not confidentiality. No TLS.
- The vario's access point SHOULD have a WPA2 password. The vario stores the secret in its flash and never returns it from any API except the pairing
  page while pairing is open. Anyone with physical access to the vario can read it.
- The vario's existing web API has no authentication and its OTA password is published in the project: that is separate from this API.

## 8. Unpairing and reset

- **By the owner, on the vario:** the owner removes the device on the vario's page (`DELETE /api/v1/sensors/{id}`, the vario's own administrative
  path, not available to clients). The vario forgets the secret; the client's next request gets 403 `not_paired`.
- **By the sensor:** `POST /api/v1/sensors/unpair` (authenticated) makes the vario forget the device; the sensor also erases the secret and `vario_id`.
  A sensor that cannot reach the vario just erases; the vario's entry stays until the owner removes it (it shows "not connected").
- A client moved to another vario needs a new pairing there; an old, unused entry on the first vario is harmless (it can no longer authenticate).
- Losing the link never unpairs (section 2).

## 9. Sensor-side endpoints (defined here, implemented by every sensor)

Every sensor MUST serve these and the vario calls them. `X-PV-Auth` as in section 4 with the `vario_id` as sender, except `status`, which a sensor MAY
answer to an unauthenticated caller with only `device_id`, `type`, `fw` and `proto`.

| Endpoint | Method | Purpose |
|---|---|---|
| `/api/v1/status` | GET | identity, health, capabilities: `{device_id,type,fw,hw,proto:{min,max},boot,epoch,up,channels:[...],batt,st,last,stored,free}` |
| `/api/v1/records` | GET | stored records after a sequence (section 6): query `epoch`, `after`, `max` (1-32) |
| `/api/v1/records/ack` | POST | durable receipt of records up to and including a sequence (section 6) |
| `/api/v1/config` | GET | what can be configured and its current values (sample interval, units shown, calibration offsets); read-only for the vario |

A sensor MAY expose further endpoints for its own administration, provisioning and updates under any other path; **the vario never depends on them.**
A sensor's answers carry the errors of section 2. The sensor MUST NOT hold a whole history in memory: records are read from its store one batch at a time.

## 10. Versioning

`proto` is one integer, here 1. The sensor sends `{min,max}`; the vario answers with the highest common version, or 426 with `supported`.
Changes within a version are additive (new optional fields; receivers ignore unknown fields).

## 11. Validation on the vario

The vario rejects, without allocating beyond its fixed buffers: a body over the limit (413), more than 16 telemetry records, a `ch`
not registered (that record is dropped; 422 if none is acceptable), a `value` outside the channel's `min..max` after scaling (stored as `st` 3 or 4
for the display, not as a reading), a `seq` at or below the acknowledged position (duplicate), a `st` outside 0-9, a counter that did not increase (401), a
`boot` that does not match the session (409). A reading is **current** while the last accepted request is younger than `stale_s` (default 10 s),
**stale** after that, the device **disconnected** after 30 s without any request, **faulty** while its latest `st` is not 0, **invalid** when a
channel has no valid value; the display never shows a stale value as current (Phase 6). The vario's flight functions never wait for any of this.

## 12. Vario capacity (implementation limits, to be confirmed by measurement)

| Item | First implementation | Designed ceiling | Claimed supported |
|---|---|---|---|
| Paired devices stored | 2 | 8 | 1 device and 1 browser (the owner's first target) until measured |
| Concurrently connected devices | 1 | 4 | 1 |
| Telemetry accepted | 1 request per 2 s per device | | |
| Channels per device | 4 | 8 | |
| Queue (fixed ring of batches) | 8 | 16 | |
| History pulled | 32 records per batch, one at a time, from a dedicated low-priority task | | |
| Request body | 1024 bytes | | |
| Retained states | one per channel | | |

The transport is HTTP only in version 1. The owner has not decided whether to add another for live telemetry; none is part of version 1.
