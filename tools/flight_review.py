#!/usr/bin/env python3
"""One simulated flight for reviewing how the volume is adjusted, with the flight log KEPT on the vario.

The HIL suite deletes its test logs when it finishes; this does not. It needs the test build on the vario (pio run -e test -t upload).

    python tools/flight_review.py [host]

The flight (about 8 minutes): ready on the ground, takeoff, a climb with the engine running (the background noise rises, so the volume
rises with it), then the engine off and a long glide (quiet again, the volume falls back), a second burst of noise, and a landing.
"Raise volume with background noise" is on for the flight and "Mute until flight" is on (the real settings, not the test ones); every
setting this changes is put back afterwards. Open the new log from the page (Logs, Replay) to see the volume chart. A copy is saved in results/.
"""
import os
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_test as h  # noqa: E402

FLIGHT_SETTINGS = dict(autoVolume=True, volumeRampPctS=40, noiseQuietDb=-52.0, noiseLoudDb=-30.0, muteUntilFlight=True,
                       speechVolume=20, varioVolume=20)

# (seconds, noise dBFS, vertical speed m/s, note): the noise is the background noise the microphone would hear
PLAN = [
    (25, -60, 3.0, "engine idling, climbing"),
    (40, -34, 3.0, "engine at full power, climbing: the volume should rise"),
    (50, -34, 2.0, "still at full power"),
    (40, -45, 0.0, "power reduced, level"),
    (60, -62, -1.0, "engine off, quiet glide: the volume should fall back"),
    (45, -38, 1.5, "a burst of power"),
    (60, -62, -1.5, "engine off again, descending"),
    (50, -62, -3.0, "descent towards landing"),
]


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "paravario.local"
    v = h.Vario(host)
    st = v.status()
    if not st.get("test"):
        sys.exit("The vario is not running the test build (pio run -e test -t upload): the simulator is not there.")
    before = {k: v.settings()[k] for k in FLIGHT_SETTINGS}
    logs0 = {f["n"] for f in v.get_json("/api/logs")["files"]}
    try:
        v.set(**FLIGHT_SETTINGS)
        m = h.on_ground_with_fix(v)
        v.sim(noise=-60)
        print("on the ground; the vario should be silent until takeoff")
        h.take_off(v, climb_to=h.GROUND_M + 400, vz=3.0)
        for seconds, noise, vz, note in PLAN:
            v.sim(noise=noise, vz=vz, to=h.GROUND_M + (400 if vz >= 0 else 2))
            t_end = time.time() + seconds
            while time.time() < t_end:
                s = v.status()
                print(f"  {note}: height {s['agl']:.0f} m, vario {s['vario']:+.1f}, volume vario {s['vol']['vario']} % speech {s['vol']['speech']} %")
                time.sleep(10)
        v.sim(speed=0, vz=-3.0, to=h.GROUND_M + 2, noise=-60)
        v.expect("APP", r"Landed after", m, timeout=120, what="landing")
        time.sleep(4)
    finally:
        v.sim(off=1, reset=1)
        v.set(**before)
    new = [f for f in v.get_json("/api/logs")["files"] if f["n"] not in logs0]
    print("logs now on the vario (kept):", [(f["n"], f["s"]) for f in v.get_json("/api/logs")["files"]])
    for f in new:
        text = h.log_text(v, f["n"])
        os.makedirs("results", exist_ok=True)
        path = os.path.join("results", "flight_review_" + f["n"])
        open(path, "w", newline="").write(text)
        print("saved a copy:", path, len(text), "bytes")
    v.stop = True


if __name__ == "__main__":
    main()
