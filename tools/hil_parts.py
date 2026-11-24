#!/usr/bin/env python3
"""Runs the whole hardware-in-the-loop suite as the three parts of the test build.

The on-device unit suites no longer fit the update slot all together, so the test build comes in three parts (PlatformIO environments
test_1, test_2, test_3, each with a third of the suites: src/test/UnitTest.h), and every test belongs to exactly one part (PARTS in
tools/hil_test.py). For each part this builds and uploads the image over Wi-Fi, waits for the vario to start, runs
`tools/hil_test.py --part N`, and keeps the output. Together the three runs are one full run.

Usage:
  python tools/hil_parts.py                    # all three parts, about 40 minutes each
  python tools/hil_parts.py --parts 3          # one part
  python tools/hil_parts.py --out some/dir -- --no-ui      # anything after -- goes to hil_test.py
  python tools/hil_parts.py --release          # put the release build back on the vario at the end

Before the run the settings and areas are exported to <out>/settings_before_run.json; after it (and after the release build is back) all flight logs on the
vario are deleted and that file is imported again.

The vario must be free for the whole run (no phone page open, nobody on its Wi-Fi: see docs/TESTING.md). The flight logs the tests make are copied to <out>/igc after each part (the vario deletes its oldest logs when space runs low) before they are deleted; with the console logs they
are in results/hil_parts by default, which git ignores: they hold the vario's real GPS position, so do not commit them.
"""
import argparse
import json
import os
import re
import subprocess
import sys
import time
import urllib.parse
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def status(host):
    with urllib.request.urlopen(f"http://{host}/api/status", timeout=6) as r:
        return json.loads(r.read())


def wait_for_vario(host, want_test, timeout=240):
    end = time.time() + timeout
    time.sleep(20)  # the restart after an upload
    while time.time() < end:
        try:
            st = status(host)
            if bool(st.get("test")) == want_test and st.get("uptime", 999) < 600:
                return st
        except Exception:  # noqa: BLE001 - not back yet
            pass
        time.sleep(4)
    raise SystemExit(f"the vario did not come back as the {'test' if want_test else 'release'} build within {timeout} s")


FILE_SKIP = ("bleName", "wifiSsid")  # this vario's own names are not carried in a settings file


def get_json(host, path):
    with urllib.request.urlopen(f"http://{host}{path}", timeout=10) as r:
        return json.loads(r.read())


def post_json(host, path, body, method="POST"):
    req = urllib.request.Request(f"http://{host}{path}", data=json.dumps(body).encode(), method=method,
                                 headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=15) as r:
        return json.loads(r.read())


def export_settings(host, path):
    """Saves the settings and areas to a file, as the /advanced Export does."""
    st = {k: v for k, v in get_json(host, "/api/settings").items() if k not in FILE_SKIP}
    doc = {"paravario": "settings", "saved": time.strftime("%Y-%m-%dT%H:%M:%S"), "settings": st, "areas": get_json(host, "/api/areas").get("a", [])}
    with open(path, "w", encoding="utf-8") as f:
        json.dump(doc, f, indent=2)
        f.write(chr(10))
    return len(st), len(doc["areas"])


def import_settings(host, path):
    """Puts a file made by export_settings back: the areas in it replace the vario's, then the settings."""
    doc = json.load(open(path, encoding="utf-8"))
    post_json(host, "/api/areas", {"a": doc["areas"]})
    post_json(host, "/api/settings", doc["settings"])
    return len(doc["settings"]), len(doc["areas"])


def delete_all_logs(host):
    n = 0
    for f in get_json(host, "/api/logs").get("files", []):
        req = urllib.request.Request(f"http://{host}/api/log?name={urllib.parse.quote(f['n'])}", method="DELETE")
        urllib.request.urlopen(req, timeout=15).read()
        n += 1
    return n


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--parts", type=int, nargs="*", default=[1, 2, 3], choices=(1, 2, 3))
    ap.add_argument("--host", default="paravario.local")
    ap.add_argument("--out", default=os.path.join(ROOT, "results", "hil_parts"))
    ap.add_argument("--release", action="store_true", help="build and upload the release build at the end")
    ap.add_argument("rest", nargs=argparse.REMAINDER, help="after --: arguments for hil_test.py")
    a = ap.parse_args()
    extra = [x for x in a.rest if x != "--"]
    os.makedirs(a.out, exist_ok=True)
    summary = []
    saved = os.path.join(a.out, "settings_before_run.json")
    try:
        ns, na = export_settings(a.host, saved)
        print(f"settings exported before the run: {ns} settings, {na} areas -> {saved}", flush=True)
    except Exception as e:  # noqa: BLE001 - without the export there is nothing to put back: stop before touching the vario
        raise SystemExit(f"could not export the settings before the run ({e}); nothing was started")
    for part in a.parts:
        env = f"test_{part}"
        print(f"\n=== part {part}: building and uploading {env}", flush=True)
        up = subprocess.run(["pio", "run", "-e", env, "-t", "upload"], cwd=ROOT, capture_output=True, text=True)
        if up.returncode != 0:
            print(up.stdout[-1500:], up.stderr[-800:])
            summary.append((part, "upload failed", None))
            continue
        st = wait_for_vario(a.host, True)
        time.sleep(15)  # let the Wi-Fi and the web server settle before the first test
        print(f"    running {st['version']} (built {st['stamp']})", flush=True)
        log = os.path.join(a.out, f"part{part}.log")
        with open(log, "w", encoding="utf-8") as f:
            p = subprocess.run([sys.executable, os.path.join(ROOT, "tools", "hil_test.py"), "--host", a.host, "--part", str(part),
                                "--save-logs", os.path.join(a.out, "igc")] + extra,
                               cwd=ROOT, stdout=f, stderr=subprocess.STDOUT, text=True)
        text = open(log, encoding="utf-8", errors="replace").read()
        m = re.findall(r"^(\d+) passed, (\d+) failed", text, re.M)
        fails = re.findall(r"^\s+FAIL\s+(\S+)", text, re.M)
        summary.append((part, f"{m[-1][0]} passed, {m[-1][1]} failed" if m else f"no result (exit {p.returncode})", fails))
        print(f"    part {part}: {summary[-1][1]}; log {log}", flush=True)
    print("\n=== summary")
    for part, res, fails in summary:
        print(f"part {part}: {res}" + (f"; failed: {', '.join(fails)}" if fails else ""))
    if a.release:
        print("\n=== putting the release build back")
        up = subprocess.run(["pio", "run", "-e", "release", "-t", "upload"], cwd=ROOT, capture_output=True, text=True)
        print("uploaded" if up.returncode == 0 else up.stdout[-1000:])
        if up.returncode == 0:
            st = wait_for_vario(a.host, False)
            print(f"running {st['version']} (built {st['stamp']})")
    try:
        n = delete_all_logs(a.host)
        ns, na = import_settings(a.host, saved)
        print(f"after the run: {n} flight logs deleted; {ns} settings and {na} areas imported from {saved}")
    except Exception as e:  # noqa: BLE001
        print(f"after the run: could not delete the logs or import the settings ({e}); the export is in {saved}")
    bad = [s for s in summary if "failed" not in s[1] or not re.search(r"\b0 failed", s[1])]
    return 1 if bad else 0


if __name__ == "__main__":
    sys.exit(main())
