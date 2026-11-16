# Sensor API diagrams (Mermaid; version 1 draft)

Rendered by GitHub and most Markdown viewers. `README.md` is the specification; these show the order of events. Sensors call the vario's endpoints;
the vario calls the sensor-side endpoints (history, acknowledge).

## Pairing

```mermaid
sequenceDiagram
    actor Owner
    participant C as Client (sensor)
    participant V as Vario
    C->>V: GET /api/v1/system/identity
    V-->>C: vario_id, name, proto range, pairing_open
    Note over C: the client shows vario_id and name so the owner can confirm
    Owner->>V: open pairing mode (vario page): a random 64-bit secret is shown (16 hex characters)
    Owner->>C: type the secret into the client (out of band: it is never sent over the air)
    C->>V: POST /pair (device_id, proto, nonce_s) with X-PV-Auth = HMAC(secret, ...)
    V->>V: recompute with the secret it is showing; on a match bind device_id to the secret (persistent)
    V-->>C: vario_id, name, mac = HMAC(secret, pair, nonce_s, device_id, vario_id)
    C->>C: check mac: this vario knows the secret it was given
    Note over V: a wrong MAC is a failed attempt: 3, or 120 s, closes pairing mode
```

## A session: register, live telemetry, history

```mermaid
sequenceDiagram
    participant C as Sensor
    participant V as Vario
    C->>V: POST /sensors/register (X-PV-Auth, session 00000000, boot, epoch, channels, nonce_s)
    V-->>C: session (new, random), intervals, acked, mac, time
    Note over C,V: later requests carry the session id and a rising counter in X-PV-Auth (one counter per direction)
    loop every telemetry_s
        C->>V: POST /sensors/telemetry (up to 16 records)
        V-->>C: rx, dup, gap, time
    end
    loop every heartbeat_s when idle
        C->>V: POST /sensors/heartbeat (last = newest seq)
        V-->>C: ok, time
    end
    Note over V: last above acked: the vario's history task pulls, one bounded batch at a time, never from the flight loop
    V->>C: GET /api/v1/records?epoch=E&after=N&max=32
    C-->>V: records N+1..M, more
    V->>V: validate, write to persistent storage, save position M
    V->>C: POST /api/v1/records/ack (epoch E, upto M)
    C-->>V: ok
    C->>C: delete or reuse records up to M
```

## History: lost acknowledgement and restart

```mermaid
sequenceDiagram
    participant C as Sensor
    participant V as Vario
    V->>C: GET /records?after=1040
    C-->>V: 1041..1072
    V->>V: store 1041..1072, save position 1072
    V--xC: POST /records/ack upto 1072 (lost)
    Note over C: still holds 1041..1072
    V->>C: GET /records?after=1072 (position from persistent storage)
    C-->>V: 1073..1090
    V->>C: POST /records/ack upto 1090
    C-->>V: ok, deletes up to 1090
    Note over V: had it asked after=1040 again, the repeated 1041..1072 would be ignored as duplicates and then acknowledged
```

## Device connection state (as seen by the vario)

```mermaid
stateDiagram-v2
    [*] --> Unknown
    Unknown --> Paired: pairing completed
    Paired --> Current: register accepted, valid telemetry
    Current --> Stale: no request for stale_s (default 10 s)
    Stale --> Current: request received
    Stale --> Disconnected: no request for 30 s
    Disconnected --> Current: register accepted again
    Current --> Fault: latest st not 0, or an invalid channel
    Fault --> Current: st back to 0
    Paired --> Unknown: removed by the owner or unpair
    Current --> Unknown: removed by the owner or unpair
    Stale --> Unknown: removed by the owner or unpair
    Disconnected --> Unknown: removed by the owner or unpair
```
