#!/usr/bin/env python3
"""ParaVario hardware-in-the-loop tests.

Runs against a vario flashed with the test build (pio run -e test -t upload), over Wi-Fi.
The test build can replace the barometer, GPS, battery voltage and background-noise inputs with
simulated values (/api/sim, see src/test/Sim.h). Each test sets up a situation, then checks what
the firmware did by reading its debug log feed (/api/debuglog). Where sound matters the real
microphone is used to confirm the speaker actually produced it (/api/spectrum).

  python tools/hil_test.py                    # all tests
  python tools/hil_test.py vario takeoff      # only tests whose name contains these words
  python tools/hil_test.py --no-audio         # skip the checks that listen with the microphone
  python tools/hil_test.py --no-ui            # skip the web page tests (ui_*), which drive the
                                              # real page in a headless Edge or Chrome
  python tools/hil_test.py --list

The vario's settings are saved first and restored afterwards, the simulation is switched off and
IGC logs made by the tests are deleted. The tests make noise (default 20 % volume).
"""
import argparse
import base64
import json
import os
import re
import shutil
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.error
import urllib.parse
import urllib.request

LINE_RE = re.compile(r"^\[\s*(\d+\.\d+)\] (\w) (\w+): (.*)$")
B_RE = re.compile(r"^B(\d{6})(\d{2})(\d{5})([NS])(\d{3})(\d{5})([EW])([AV])(\d{5})(\d{5})$")


class Fail(Exception):
    pass


class LogLine:
    def __init__(self, t, level, tag, msg):
        self.t, self.level, self.tag, self.msg = t, level, tag, msg

    def __repr__(self):
        return f"[{self.t:9.3f}] {self.level} {self.tag}: {self.msg}"


class Vario:
    def __init__(self, host, verbose=False):
        self.base = f"http://{host}"
        self.verbose = verbose
        self.lines = []
        self.next_seq = 0
        self.dropped = 0
        self.browser = None
        self.lock = threading.Lock()
        self.stop = False
        # The vario keeps only ~20 s of log, so it is read continuously in the background.
        threading.Thread(target=self._pump, daemon=True).start()

    def _pump(self):
        while not self.stop:
            try:
                self.poll()
            except Exception:
                pass
            time.sleep(1.2)  # gently: the vario serves one request at a time

    def page(self):
        """The vario's web page, freshly loaded in the headless browser."""
        if self.browser is None:
            self.browser = Browser()
        # By address: the browser's own name lookup of paravario.local can be slow or missing.
        host = self.base[len("http://"):]
        with self.lock:  # no log polling while the page loads
            self.browser.open("http://" + socket.gethostbyname(host) + "/")
        return self.browser

    # ---- HTTP
    def _open(self, path, data=None, method=None, timeout=8, tries=4):
        last = None
        for attempt in range(tries):
            try:
                req = urllib.request.Request(self.base + path, data=data, method=method)
                if data is not None:
                    req.add_header("Content-Type", "application/json")
                with urllib.request.urlopen(req, timeout=timeout) as r:
                    return r.status, r.read()
            except urllib.error.HTTPError as e:
                return e.code, e.read()
            except Exception as e:  # Wi-Fi hiccup: retry
                last = e
                time.sleep(0.5 + attempt)
        raise Fail(f"vario not reachable ({path}): {last}")

    def get_json(self, path):
        return json.loads(self._open(path)[1])

    def status(self):
        return self.get_json("/api/status")

    def settings(self):
        return self.get_json("/api/settings")

    def set(self, **kv):
        code, body = self._open("/api/settings", data=json.dumps(kv).encode(), method="POST")
        if code != 200:
            raise Fail(f"settings rejected: {body[:100]}")
        return json.loads(body)["settings"]

    def sim(self, **kv):
        code, body = self._open("/api/sim?" + urllib.parse.urlencode(kv))
        if code != 200:
            raise Fail(f"sim {kv} rejected: {body[:100]}")
        return json.loads(body)

    def action(self, what, expect=200):
        code, body = self._open("/api/action?do=" + what, data=b"", method="POST")
        if code != expect:
            raise Fail(f"action {what}: HTTP {code} {body[:80]}")

    # ---- log feed
    def poll(self):
        with self.lock:
            self._poll()

    def _poll(self):
        code, body = self._open(f"/api/debuglog?since={self.next_seq}")
        text = body.decode("utf-8", "replace").split("\n")
        m = re.match(r"#next=(\d+) dropped=(\d+)", text[0])
        if not m:
            return
        nxt = int(m.group(1))
        if nxt < self.next_seq:  # vario restarted
            self.next_seq = 0
            return self._poll()
        if self.next_seq:  # the first read starts mid-stream; that is not a loss
            self.dropped += int(m.group(2))
        self.next_seq = nxt
        for raw in text[1:]:
            lm = LINE_RE.match(raw)
            if lm:
                line = LogLine(float(lm.group(1)), lm.group(2), lm.group(3), lm.group(4))
                self.lines.append(line)
                if self.verbose:
                    print("      " + repr(line))

    def mark(self):
        self.poll()
        return len(self.lines)

    def find(self, tag, pattern, since=0):
        rx = re.compile(pattern)
        for line in self.lines[since:]:
            if line.tag == tag:
                m = rx.search(line.msg)
                if m:
                    return line, m
        return None

    def expect(self, tag, pattern, since, timeout=10.0, what=None):
        """Waits for a log line with this tag matching the pattern. Returns (line, match)."""
        end = time.time() + timeout
        while True:
            self.poll()
            hit = self.find(tag, pattern, since)
            if hit:
                return hit
            if time.time() > end:
                recent = "\n        ".join(repr(l) for l in self.lines[since:] if l.tag == tag)[-900:]
                raise Fail(f"no log line {tag}: /{pattern}/ within {timeout:.0f} s"
                           f"{' (' + what + ')' if what else ''}\n        {tag} lines seen: {recent or 'none'}")
            time.sleep(0.4)

    def never(self, tag, pattern, since, duration):
        """Fails if a matching line appears during the next `duration` seconds."""
        end = time.time() + duration
        while time.time() < end:
            self.poll()
            hit = self.find(tag, pattern, since)
            if hit:
                raise Fail(f"unexpected log line {hit[0]!r}")
            time.sleep(0.4)

    def nav(self, since=None, timeout=6.0):
        """The next complete NAV trace after `since` as a dict of its key=value fields."""
        if since is None:
            since = self.mark()
        end = time.time() + timeout
        while True:
            self.poll()
            out, seen = {}, 0
            for line in self.lines[since:]:
                if line.tag != "NAV":
                    continue
                if line.msg.startswith("fix="):
                    out, seen = {}, 0
                for k, v in re.findall(r"(\w+)=(\S+)", line.msg):
                    out[k] = v
                pos = re.search(r"hdop=\S+ (-?[\d.]+),(-?[\d.]+)", line.msg)
                if pos:
                    out["lat"], out["lon"] = pos.group(1), pos.group(2)
                seen += 1
                if seen == 3 and "mode" in out:
                    return out
            if time.time() > end:
                raise Fail("no NAV trace in the log")
            time.sleep(0.4)

    def wait_nav(self, cond, timeout, what):
        end = time.time() + timeout
        last = None
        while time.time() < end:
            last = self.nav()
            try:
                if cond(last):
                    return last
            except (KeyError, ValueError):
                pass
        raise Fail(f"{what} not reached within {timeout:.0f} s (last NAV: {last})")

    # ---- microphone
    def spectrum(self):
        code, body = self._open("/api/spectrum?since=0")
        if code != 200 or len(body) < 12 + 512:
            return None
        seq, bin_hz, tone_hz = struct.unpack("<Iff", body[:12])
        return bin_hz, [b / 2.5 - 100.0 for b in body[12:12 + 512]]

    def listen(self, seconds=2.0):
        """Peak-hold spectrum over a few seconds (beeps are on only half the time)."""
        # The vario only computes spectra while someone asks, so the first takes a moment.
        first = time.time() + 5
        while self.spectrum() is None and time.time() < first:
            time.sleep(0.2)
        end = time.time() + seconds
        best, bin_hz = None, 0
        while time.time() < end:
            sp = self.spectrum()
            if sp:
                bin_hz, db = sp
                best = db if best is None else [max(a, b) for a, b in zip(best, db)]
            time.sleep(0.2)
        if best is None:
            raise Fail("no microphone spectrum from the vario")
        return bin_hz, best


def kill_stray_browsers():
    """Stop test browsers a killed earlier run left behind. Each keeps the vario's page open and
    polling, which loads the vario for hours and spoils memory checks and firmware uploads."""
    try:
        if os.name == "nt":
            subprocess.run(
                ["powershell", "-NoProfile", "-Command",
                 "Get-CimInstance Win32_Process | Where-Object { $_.CommandLine -like "
                 "'*user-data-dir=*paravario-ui-*' } | ForEach-Object { Stop-Process -Id "
                 "$_.ProcessId -Force -ErrorAction SilentlyContinue }"],
                stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
        else:
            subprocess.run(["pkill", "-f", "user-data-dir=.*paravario-ui-"],
                           stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL, timeout=30)
    except Exception:
        pass


class Browser:
    """A headless Edge or Chrome showing the vario's web page, driven over the DevTools protocol
    with nothing but the standard library (no Selenium/Playwright install needed)."""

    CANDIDATES = [
        r"C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Microsoft\Edge\Application\msedge.exe",
        r"C:\Program Files\Google\Chrome\Application\chrome.exe",
        r"C:\Program Files (x86)\Google\Chrome\Application\chrome.exe",
        "/Applications/Google Chrome.app/Contents/MacOS/Google Chrome",
    ]

    def __init__(self):
        exe = next((p for p in self.CANDIDATES if os.path.exists(p)), None)
        exe = exe or next((shutil.which(n) for n in ("msedge", "google-chrome", "chromium", "chrome")
                           if shutil.which(n)), None)
        if not exe:
            raise Fail("no Edge or Chrome found for the web page tests (use --no-ui to skip them)")
        self.dir = tempfile.mkdtemp(prefix="paravario-ui-")
        with socket.socket() as s:
            s.bind(("127.0.0.1", 0))
            port = s.getsockname()[1]
        self.proc = subprocess.Popen(
            [exe, "--headless=new", f"--remote-debugging-port={port}", f"--user-data-dir={self.dir}",
             "--no-first-run", "--no-default-browser-check", "--disable-gpu", "--window-size=420,900",
             "about:blank"], stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        end = time.time() + 20
        ws_url = None
        while time.time() < end and not ws_url:
            try:
                with urllib.request.urlopen(f"http://127.0.0.1:{port}/json", timeout=2) as r:
                    pages = [t for t in json.loads(r.read()) if t.get("type") == "page"]
                    ws_url = pages[0]["webSocketDebuggerUrl"] if pages else None
            except Exception:
                time.sleep(0.3)
        if not ws_url:
            self.close()
            raise Fail("could not start the headless browser")
        u = urllib.parse.urlparse(ws_url)
        self.sock = socket.create_connection((u.hostname, u.port), timeout=20)
        key = base64.b64encode(os.urandom(16)).decode()
        self.sock.sendall((f"GET {u.path} HTTP/1.1\r\nHost: {u.hostname}:{u.port}\r\nUpgrade: websocket\r\n"
                           f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\n"
                           "Sec-WebSocket-Version: 13\r\n\r\n").encode())
        head = b""
        while b"\r\n\r\n" not in head:
            head += self.sock.recv(1)
        if b" 101 " not in head.split(b"\r\n")[0]:
            raise Fail("browser refused the DevTools connection")
        self.next_id = 0

    def _read(self, n):
        buf = b""
        while len(buf) < n:
            chunk = self.sock.recv(n - len(buf))
            if not chunk:
                raise Fail("browser connection closed")
            buf += chunk
        return buf

    def _send(self, text, opcode=1):
        data = text.encode() if isinstance(text, str) else text
        mask = os.urandom(4)
        n = len(data)
        head = bytes([0x80 | opcode]) + (bytes([0x80 | n]) if n < 126 else
                                         bytes([0x80 | 126]) + struct.pack(">H", n) if n < 65536 else
                                         bytes([0x80 | 127]) + struct.pack(">Q", n))
        self.sock.sendall(head + mask + bytes(b ^ mask[i % 4] for i, b in enumerate(data)))

    def _recv(self):
        message = b""
        while True:
            b0, b1 = self._read(2)
            n = b1 & 0x7F
            if n == 126:
                n = struct.unpack(">H", self._read(2))[0]
            elif n == 127:
                n = struct.unpack(">Q", self._read(8))[0]
            data = self._read(n)
            op = b0 & 0x0F
            if op == 9:
                self._send(data, opcode=10)
                continue
            if op == 8:
                raise Fail("browser closed the DevTools connection")
            message += data
            if b0 & 0x80:
                return json.loads(message)

    def cmd(self, method, **params):
        self.next_id += 1
        self._send(json.dumps({"id": self.next_id, "method": method, "params": params}))
        while True:
            msg = self._recv()
            if msg.get("id") == self.next_id:
                if "error" in msg:
                    raise Fail(f"browser: {method}: {msg['error'].get('message')}")
                return msg.get("result", {})

    def js(self, expression):
        """Evaluates JavaScript in the page and returns its (JSON-able) value."""
        r = self.cmd("Runtime.evaluate", expression=expression, returnByValue=True, awaitPromise=True)
        if "exceptionDetails" in r:
            d = r["exceptionDetails"]
            raise Fail(f"page script error: {d.get('exception', {}).get('description') or d.get('text')}"
                       f" in: {expression[:80]}")
        return r["result"].get("value")

    def open(self, url):
        # The page loads its settings once and, in a debug build, ends its start-up by switching
        # to the system tab: wait for that so it cannot undo a tab the test selects. A page whose
        # settings request failed stays empty, so reload it (see the note in the test report).
        for attempt in range(3):
            # Let the page being left finish its requests first: a browser abandoning requests
            # half-way leaves the vario's one-at-a-time web server waiting on dead connections.
            try:
                self.js("window.fetch=()=>new Promise(()=>{});window.specOn=false;1")
                time.sleep(1.5)
            except Fail:
                pass
            self.cmd("Page.navigate", url=url)
            try:
                self.wait("document.readyState==='complete' && typeof poll==='function' && schema.length>0"
                          " && !document.getElementById('systemPage').hidden", 10, "web page loaded with its settings")
                break
            except Fail:
                self.reloads = getattr(self, "reloads", 0) + 1
                if attempt == 2:
                    raise
        self.js("window.confirm=()=>true;" + PAGE_HELPERS)

    def wait(self, expression, timeout, what):
        end = time.time() + timeout
        while True:
            try:
                if self.js(expression):
                    return
            except Fail:
                if time.time() > end:
                    raise
            if time.time() > end:
                raise Fail(f"web page: {what} (not within {timeout:.0f} s)")
            time.sleep(0.25)

    def text(self, element_id):
        return self.js(f"document.getElementById({json.dumps(element_id)}).textContent")

    def wait_text(self, element_id, pattern, timeout=10, what=None):
        """Waits until the element's text matches the regular expression; returns the text."""
        end = time.time() + timeout
        while True:
            t = self.text(element_id)
            if re.search(pattern, t):
                return t
            if time.time() > end:
                raise Fail(f"web page: #{element_id} shows '{t}', expected /{pattern}/"
                           f"{' (' + what + ')' if what else ''}")
            time.sleep(0.25)

    def click(self, selector):
        if not self.js(f"(e=>{{if(!e)return false;e.click();return true}})(document.querySelector({json.dumps(selector)}))"):
            raise Fail(f"web page: nothing to click at {selector}")

    def tab(self, name):
        self.click(f'button[data-tab="{name}"]')
        self.wait(f"!document.getElementById('{name}Page').hidden", 3, f"{name} tab shown")

    def check_fits(self, where):
        if self.js("document.documentElement.scrollWidth > window.innerWidth + 1"):
            raise Fail(f"web page: {where} is wider than a phone screen (scrolls sideways)")

    def close(self):
        try:
            self.cmd("Browser.close")
        except Exception:
            pass
        try:
            self.proc.terminate()
            self.proc.wait(timeout=5)
        except Exception:
            pass
        shutil.rmtree(self.dir, ignore_errors=True)


# Helpers injected into the page: find a setting's input by its key, set it like a user would.
PAGE_HELPERS = """
window.tInput=k=>{const f=schema.find(f=>f.k===k);
  for(const d of document.querySelectorAll('#settings details')){
    if(d.querySelector('summary').firstChild.textContent!==f.g)continue;
    for(const r of d.querySelectorAll('label.row')){
      if(r.querySelector('span').firstChild.textContent===f.l)return r.querySelector('input,select')}}
  return null};
window.tRow=k=>tInput(k).closest('label.row');
window.tSet=(k,v)=>{const i=tInput(k);if(i.type==='checkbox')i.checked=v;else i.value=v;
  i.dispatchEvent(new Event('change'));return true};
"""


def level_at(bin_hz, db, hz, width=25.0):
    lo, hi = int((hz - width) / bin_hz), int((hz + width) / bin_hz) + 1
    return max(db[max(lo, 1):min(hi, len(db))])


def floor_of(db):
    s = sorted(db[20:])
    return s[len(s) // 2]


def band_mean(bin_hz, db, lo_hz, hi_hz):
    a, b = int(lo_hz / bin_hz), int(hi_hz / bin_hz)
    seg = db[a:b]
    return sum(seg) / len(seg)


def band_top(bin_hz, db, lo_hz, hi_hz, n=20):
    seg = sorted(db[int(lo_hz / bin_hz):int(hi_hz / bin_hz)], reverse=True)[:n]
    return sum(seg) / len(seg)


def near(value, target, tol, what):
    if abs(float(value) - target) > tol:
        raise Fail(f"{what}: got {value}, expected {target} +/- {tol}")


# --------------------------------------------------------------------------------------------
# Settings the tests run with (the vario's own are restored afterwards).
TEST_SETTINGS = dict(
    heightUnit=0, distUnit=0, speedUnit=0, autoVolume=False, volumeRampPctS=500, glideShortS=1, glideLongS=20, noiseQuietDb=-50, noiseLoudDb=-30,
    varioEnabled=True, varioOnlyInFlight=False, varioClimbBeeps=True, varioSinkAlarm=True,
    climbThreshold=0.3, sinkThreshold=-2.0, varioBaseHz=700, varioHzPerMs=100, varioAccelNoise=1.0,
    boundariesUp="15.24,30.48", boundaries="15.24,30.48", boundaryStepM=152.4, boundaryHystM=3.0,
    boundaryClimbing=True, boundaryDescending=True, heightIntervalS=0, flightTimeIntervalMin=0,
    takeoffSpeedMs=4.0, takeoffTimeS=3, takeoffGainM=5.0, baroTakeoffGainM=15.0,
    groundHoldVarioMs=0.5, landingSpeedMs=1.4, landingTimeS=5,
    lzMode=0, windManual=False, readyMinSats=6, readyMaxHdop=2.0, guidanceUpdateS=20, emergencyUpdateS=30,
    onCourseDeg=15, offHeadingDeg=30, battLowV=3.5, battFullV=4.09, battRuntimeMin=360,
    igcEnabled=True, igcPreS=20, igcQnhAlt=True,
)

GROUND_M = 100.0  # simulated field elevation


def reset(v, **sim):
    """Simulation off, flight logic back to power-on state, then the given simulated inputs."""
    m = v.mark()
    # Step 1: everything simulated except GPS, which goes silent, and the flight logic reset in
    # the same request (so the jump from the previous altitude cannot look like a takeoff).
    first = {k: sim[k] for k in ("baro", "alt", "vz", "baroOffset") if k in sim}
    v.sim(off=1, **first, gps="silent", reset=1)
    time.sleep(3.3)  # the last fix of the previous situation expires (it is trusted for 3 s)
    # Step 2: the full situation, and a clean flight state to start it from.
    v.sim(**{k: val for k, val in sim.items() if k not in first}, **({} if "gps" in sim else {"gps": "real"}),
          reset=1)
    return m  # log position from before the reset, so nothing it triggers is missed


def on_ground_with_fix(v):
    """Simulated barometer and a good GPS fix, standing still at the field. Waits until ready."""
    m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="fix", sats=10, hdop=0.9,
              speed=0, track=90, lat=51.5, lon=0.5)
    v.expect("RDY", r"^ready$", m, timeout=25, what="ready to fly with a good fix")
    return m


def take_off(v, climb_to=GROUND_M + 60, vz=2.0):
    m = v.mark()
    v.sim(speed=8, track=90)  # takeoff run: the sudden speed is "suspect" for 3 s before it counts
    time.sleep(5)
    v.sim(vz=vz, to=climb_to)
    v.expect("FLT", r"^takeoff \(gps\)", m, timeout=15, what="takeoff detected")
    return m


# --------------------------------------------------------------------------------------------
def t_boot(v, a):
    """Restart: build is the test build, start-up beep then 'Ready' a second later.

    The vario times its own power-on sequence and reports it in the status reply. The debug log
    cannot be relied on here: the vario may take a good while to rejoin the Wi-Fi after a restart
    (especially right after a firmware flash), and by then the log has moved on."""
    before = v.status()["uptime"]
    v.action("restart")
    time.sleep(4)
    end = time.time() + 90
    st = None
    while True:
        try:
            st = json.loads(v._open("/api/status", tries=1)[1])
            if st["uptime"] < before or st["uptime"] < 30:
                break
        except Fail:
            pass
        if time.time() > end:
            raise Fail("vario did not come back after restart")
        time.sleep(2)
    if not st.get("test"):
        raise Fail("vario is not running the test build (pio run -e test -t upload)")
    if not st["debug"]:
        raise Fail("the test build should report debug = true")
    b = st["boot"]
    if not b["done"]:
        raise Fail(f"power-on sequence not finished after {st['uptime']} s: {b}")
    near(b["hz"], 900, 5, "start-up beep pitch (2 m/s climb)")
    if not 200 <= b["beepMs"] <= 450:
        raise Fail(f"beep lasted {b['beepMs'] / 1000:.2f} s, expected about 0.25 s")
    if not 950 <= b["readyGapMs"] <= 1300:
        raise Fail(f"'Ready' came {b['readyGapMs'] / 1000:.2f} s after the beep, expected about 1 s")
    return (f"beep {b['beepMs'] / 1000:.2f} s at {b['hz']:.0f} Hz, 'Ready' {b['readyGapMs'] / 1000:.2f} s later "
            f"(vario answered again after {st['uptime']} s)")


def t_settings(v, a):
    """Settings API: values round-trip and out-of-range values are clamped."""
    s = v.set(climbThreshold=0.35, speechVolume=500, takeoffTimeS=-4)
    near(s["climbThreshold"], 0.35, 0.001, "climbThreshold round trip")
    near(s["speechVolume"], 100, 0, "speechVolume clamped to maximum")
    if s["takeoffTimeS"] < 0:
        raise Fail("takeoffTimeS accepted a negative value")
    near(v.settings()["climbThreshold"], 0.35, 0.001, "climbThreshold read back")
    v.set(climbThreshold=0.3, speechVolume=a.volume, takeoffTimeS=3)
    return "round trip and clamping ok"


def no_baro_takeoff(fn):
    """Runs a bench climb test with the no-GPS takeoff out of reach, so a long simulated climb
    on the 'ground' is not taken for a flight."""
    def wrapped(v, a):
        v.set(baroTakeoffGainM=200.0)
        try:
            return fn(v, a)
        finally:
            v.set(baroTakeoffGainM=TEST_SETTINGS["baroTakeoffGainM"])
    wrapped.__doc__, wrapped.__name__ = fn.__doc__, fn.__name__
    return wrapped


@no_baro_takeoff
def t_vario_climb(v, a):
    """1 m/s climb: beeping tone at 800 Hz in the log and heard by the microphone."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    time.sleep(2)
    quiet = v.listen(1.5) if a.audio else None
    m = v.mark()
    v.sim(vz=1.0)
    v.wait_nav(lambda n: abs(float(n["vs"]) - 1.0) < 0.12, 12, "vario reading 1.0 m/s")
    m2 = v.mark()
    _, tm = v.expect("TONE", r"^climb (\d+) Hz", m, timeout=6)
    hz = float(v.lines[max(i for i, l in enumerate(v.lines) if l.tag == "TONE")].msg.split()[1])
    near(hz, 800, 15, "climb tone pitch")
    note = f"tone {hz:.0f} Hz"
    if a.audio:
        bin_hz, db = v.listen(2.5)
        got, fl = level_at(bin_hz, db, hz), floor_of(db)
        before = level_at(quiet[0], quiet[1], hz)
        if got - fl < 12 or got - before < 10:
            raise Fail(f"microphone did not hear the {hz:.0f} Hz tone: {got:.0f} dBFS, floor {fl:.0f}, "
                       f"before {before:.0f}")
        note += f", heard at {got:.0f} dBFS ({got - before:+.0f} dB on silence)"
    v.sim(vz=0)
    v.expect("TONE", r"^off", m2, timeout=8, what="tone stops when the climb stops")
    return note


def t_vario_sink(v, a):
    """3 m/s sink: continuous 290 Hz alarm in the log and heard by the microphone."""
    reset(v, baro="on", alt=GROUND_M + 300, vz=0)
    time.sleep(2)
    m = v.mark()
    v.sim(vz=-3.0)
    v.wait_nav(lambda n: abs(float(n["vs"]) + 3.0) < 0.2, 12, "vario reading -3.0 m/s")
    v.expect("TONE", r"^sink (\d+) Hz", m, timeout=6)
    hz = float([l for l in v.lines if l.tag == "TONE"][-1].msg.split()[1])
    near(hz, 290, 12, "sink tone pitch")
    note = f"tone {hz:.0f} Hz"
    if a.audio:
        bin_hz, db = v.listen(2.0)
        got, fl = level_at(bin_hz, db, hz), floor_of(db)
        if got - fl < 12:
            raise Fail(f"microphone did not hear the {hz:.0f} Hz alarm: {got:.0f} dBFS, floor {fl:.0f}")
        note += f", heard at {got:.0f} dBFS ({got - fl:.0f} dB over floor)"
    m2 = v.mark()
    v.sim(vz=0)
    v.expect("TONE", r"^off", m2, timeout=8)
    return note


@no_baro_takeoff
def t_mute(v, a):
    """Mute silences the vario tone and says so; unmute brings it back."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    time.sleep(1.5)
    m = v.mark()
    v.action("mute")
    v.expect("SAY", r"\[urgent\] vario_muted$", m, timeout=4)
    v.sim(vz=1.5)
    v.wait_nav(lambda n: float(n["vs"]) > 1.2 and n["mute"] == "1", 10, "climbing while muted")
    v.never("TONE", r"^climb", m, 2.0)
    m2 = v.mark()
    v.action("mute")
    v.expect("SAY", r"\[urgent\] vario_on$", m2, timeout=4)
    v.expect("TONE", r"^climb", m2, timeout=6, what="tone returns after unmute")
    v.sim(vz=0)
    return "muted climb silent, tone back after unmute"


def t_auto_volume(v, a):
    """Background noise raises both volumes between the two thresholds."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    v.set(autoVolume=True, noiseQuietDb=-50, noiseLoudDb=-30, speechVolume=20, varioVolume=40)
    try:
        out = []
        for noise, sp, va in ((-60, 20, 40), (-40, 60, 70), (-20, 100, 100)):
            v.sim(noise=noise)
            m = v.mark()
            _, mm = v.expect("VOL", rf"noise {noise:.1f} dBFS, auto on: speech (\d+)% \(set 20%, ([+-]\d+)\), "
                             r"vario (\d+)% \(set 40%", m, timeout=8)
            near(mm.group(1), sp, 1, f"speech volume at {noise} dBFS")
            near(mm.group(3), va, 1, f"vario volume at {noise} dBFS")
            out.append(f"{noise} dBFS -> {mm.group(1)}/{mm.group(3)}%")
        v.set(autoVolume=False)
        v.sim(noise=-20)
        m = v.mark()
        _, mm = v.expect("VOL", r"auto off: speech (\d+)%", m, timeout=8)
        near(mm.group(1), 20, 0, "speech volume with auto-volume off")
    finally:
        v.sim(noise="off")
        v.set(autoVolume=False, speechVolume=a.volume, varioVolume=a.volume)
    return ", ".join(out)


def t_gps_states(v, a):
    """GPS absent, no fix, weak fix (bad speed and altitude ignored) and good fix."""
    m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=60, gps="silent")
    v.expect("RDY", r"GPS not responding", m, timeout=6)
    v.sim(gps="nofix")
    v.expect("RDY", r"Waiting for satellites", m, timeout=6)
    # Weak indoor fix: 4 satellites, 12 m/s of speed noise and a 38 km altitude.
    weak = v.mark()
    v.sim(gps="fix", sats=4, hdop=4.2, speed=12, gpsAlt=38000)
    n = v.wait_nav(lambda n: n["fix"] == "1" and n["sat"] == "4", 8, "weak fix seen")
    time.sleep(3)
    n = v.nav()
    near(n["gs"], 0, 0.05, "speed from a weak fix must read 0")
    if float(n["msl"]) > 500 or abs(float(n["agl"])) > 20:
        raise Fail(f"weak-fix altitude leaked into the height: msl {n['msl']} agl {n['agl']}")
    if v.find("RDY", r"^ready$", weak):
        raise Fail("reported ready on a weak fix")
    # Good fix, standing still.
    v.sim(sats=10, hdop=0.9, speed=0, gpsAltErr=0)
    v.expect("SAY", r"\] gps_fix$", m, timeout=8)
    v.expect("FLT", r"GPS altitude datum set", m, timeout=8)
    v.expect("RDY", r"^ready$", m, timeout=15, what="ready after a steady good fix")
    n = v.wait_nav(lambda n: abs(float(n["msl"]) - GROUND_M) < 3, 8, "height matches GPS altitude")
    if n["lz"] != "1":
        raise Fail("landing zone not set from the good fix")
    # A wildly wrong GPS altitude on an otherwise good fix must not move the height.
    v.sim(gpsAlt=3000)
    time.sleep(4)
    n = v.nav()
    near(n["msl"], GROUND_M, 5, "height after a GPS altitude jump on a good fix")
    v.sim(gpsAltErr=0, speed=12)
    n = v.wait_nav(lambda n: abs(float(n["gs"]) - 12) < 0.3, 6, "speed from a good fix")
    return "not responding / waiting / weak fix ignored / good fix ready"


def t_flight(v, a):
    """Takeoff, height calls up and down, GPS loss, landing and the IGC log of it all."""
    before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    on_ground_with_fix(v)
    time.sleep(21)  # fill the pre-takeoff buffer (igcPreS = 20)
    m = take_off(v, climb_to=GROUND_M + 60)
    tk, _ = v.expect("SAY", r"\[high\] takeoff$", m, timeout=4)
    _, lg = v.expect("IGC", r"Logging to (\S+) \((\d+) s of pre-takeoff history\)", m, timeout=4)
    if int(lg.group(2)) < 18:
        raise Fail(f"only {lg.group(2)} s of pre-takeoff history written, expected about 20")
    v.expect("SAY", r"\[normal\] n50 feet$", m, timeout=15, what="climbing through 50 ft")
    v.expect("SAY", r"\[normal\] n1 hundred feet$", m, timeout=15, what="climbing through 100 ft")
    v.expect("SIM", r"reached target altitude 160", m, timeout=30)
    n = v.wait_nav(lambda n: abs(float(n["agl"]) - 60) < 3 and n["fly"] == "1", 8, "level at 60 m above ground")
    near(n["msl"], 160, 4, "height above sea level")

    # GPS lost in flight (a gentle climb meanwhile: level flight without a fix counts as landed).
    m2 = v.mark()
    v.sim(gps="nofix", vz=0.6)
    v.expect("SAY", r"\[high\] no_gps$", m2, timeout=12, what="'No GPS' after 5 s without a fix")
    v.sim(gps="fix", vz=0)
    v.wait_nav(lambda n: n["good"] == "1" and n["fly"] == "1", 10, "fix recovered, still flying")
    top = v.sim()["alt"]

    # Descend through the same boundaries.
    m3 = v.mark()
    v.sim(vz=-3, to=GROUND_M + 2)
    v.expect("TONE", r"^sink", m3, timeout=6)
    v.expect("SAY", r"\[normal\] n1 hundred feet$", m3, timeout=15, what="descending through 100 ft")
    v.expect("SAY", r"\[normal\] n50 feet$", m3, timeout=15, what="descending through 50 ft")
    v.expect("SIM", r"reached target altitude 102", m3, timeout=30)

    # Land.
    m4 = v.mark()
    v.sim(speed=0)
    v.expect("APP", r"Landed after (\d+) s", m4, timeout=15, what="landing detected")
    v.expect("SAY", r"\[high\] landed flight_time n\d+ minutes?$", m4, timeout=4)
    v.wait_nav(lambda n: n["fly"] == "0", 5, "flight ended")
    if v.status()["logging"]:
        raise Fail("IGC log still open after landing")

    # The IGC file.
    files = [f["n"] for f in v.get_json("/api/logs")["files"] if f["n"] not in before]
    if len(files) != 1:
        raise Fail(f"expected one new IGC file, found {files}")
    code, body = v._open("/api/log?name=" + urllib.parse.quote(files[0]))
    text = body.decode("ascii", "replace")
    rows = text.split("\r\n")
    if rows[0] != "AXPVParaVario" or not any(r.startswith("HFDTEDATE:010121") for r in rows):
        raise Fail("IGC header wrong")
    bad = [r for r in rows if r.startswith("B") and not B_RE.match(r)]
    if bad:
        raise Fail(f"malformed B record (negative altitude?): {bad[0]}")
    b = [B_RE.match(r) for r in rows if r.startswith("B")]
    times = [int(x.group(1)[:2]) * 3600 + int(x.group(1)[2:4]) * 60 + int(x.group(1)[4:]) for x in b]
    if any(t2 <= t1 for t1, t2 in zip(times, times[1:])):
        raise Fail("IGC times not strictly increasing")
    press = [int(x.group(9)) for x in b]
    gpsalt = [int(x.group(10)) for x in b]
    # The simulated barometer reads 150 m below true altitude, so a true pressure altitude
    # would be negative on the ground: the file must hold the GPS-referenced value instead.
    near(press[0], GROUND_M, 4, "first IGC pressure altitude (ground, GPS-referenced)")
    near(max(press), top, 4, "highest IGC pressure altitude")
    near(max(gpsalt), top, 2, "highest IGC GPS altitude")
    lons = [int(x.group(5)) * 60000 + int(x.group(6)) for x in b]
    if lons[-1] - lons[0] < 100:
        raise Fail("IGC track did not move east")
    still = sum(1 for i in range(1, len(lons)) if lons[i] == lons[0])
    if still < 6:
        raise Fail(f"IGC file has only {still} s of stationary pre-takeoff track")
    voids = sum(1 for x in b if x.group(8) == "V")
    if not 3 <= voids <= 12:
        raise Fail(f"expected a few 'V' (no fix) records from the GPS loss, found {voids}")
    v._open("/api/log?name=" + urllib.parse.quote(files[0]), method="DELETE")
    return f"{len(b)} IGC records, {still} s before takeoff, alt {min(press)}..{max(press)} m, {voids} without fix"


def t_baro_only(v, a):
    """No GPS at all: takeoff from height gain alone, landing from a steady vario.

    The ground level follows a slow change (pressure drift, walking up a slope) but is held
    during a real climb, so a gentle 2 m/s climb-out is detected and height above ground is
    measured from where the climb began."""
    before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    reset(v, baro="on", alt=GROUND_M, vz=0, gps="silent")
    time.sleep(7)  # ground reference settles
    m = v.mark()
    v.sim(vz=0.3, to=GROUND_M + 4)  # slower than the hold rate: the ground level follows
    v.expect("SIM", r"reached target", m, timeout=20)
    time.sleep(8)
    if v.find("FLT", r"^takeoff", m):
        raise Fail("a slow 4 m rise was taken for a takeoff")
    n = v.nav()
    if abs(float(n["agl"])) > 1.5:
        raise Fail(f"ground level did not follow a slow rise: agl {n['agl']} m")
    m = v.mark()
    v.sim(vz=2, to=GROUND_M + 64)
    v.expect("FLT", r"^takeoff \(baro only\)", m, timeout=14, what="takeoff in a 2 m/s climb without GPS")
    v.expect("SAY", r"\[high\] takeoff$", m, timeout=4)
    v.expect("SAY", r"\[normal\] n1 hundred feet$", m, timeout=20, what="height call without GPS")
    v.expect("SIM", r"reached target", m, timeout=40)
    n = v.wait_nav(lambda n: 56 < float(n["agl"]) < 63 and abs(float(n["vs"])) < 0.2, 8,
                   "height above ground from barometer alone (true value 60 m)")
    m2 = v.mark()
    v.expect("APP", r"Landed after", m2, timeout=20, what="landing from steady vario (2 x landing time)")
    for f in v.get_json("/api/logs")["files"]:
        if f["n"] not in before:
            v._open("/api/log?name=" + urllib.parse.quote(f["n"]), method="DELETE")
    return f"slow rise ignored, takeoff at 2 m/s and landing without GPS, agl reads {n['agl']} m at a true 60 m"


def t_guidance(v, a):
    """Approach and emergency: refusals, start-up calls, on-course call, cancel."""
    reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=60, gps="nofix")
    time.sleep(3.5)
    m = v.mark()
    v.action("emergency")
    v.expect("SAY", r"\[urgent\] no_gps$", m, timeout=4, what="emergency refused without a fix")
    on_ground_with_fix(v)
    m = v.mark()
    v.action("approach")
    v.expect("SAY", r"\[urgent\] wind_unknown$", m, timeout=4, what="approach refused without wind")

    before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    m0 = v.mark()
    take_off(v, climb_to=GROUND_M + 100, vz=4)
    v.expect("SIM", r"reached target", m0, timeout=40)
    # Flying east away from the field: the landing zone is behind, at 6 o'clock.
    m = v.mark()
    v.action("emergency")
    v.expect("EMG", r"^start: agl (\d+) m", m, timeout=4)
    v.expect("SAY", r"\[urgent\] emergency glide_range .* landing_zone n6 oclock ", m, timeout=4)
    v.wait_nav(lambda n: n["mode"].startswith("emergency"), 5, "emergency mode")
    v.sim(track=270)  # turn back towards the field
    v.expect("SAY", r"\[high\] on_course$", m, timeout=10)
    m2 = v.mark()
    v.action("emergency")
    v.expect("SAY", r"\[urgent\] emergency_off$", m2, timeout=4)

    m3 = v.mark()
    v.action("approach")  # wind is known now, from the takeoff run
    v.expect("APR", r"^Approach: upwind (\d+)", m3, timeout=4)
    v.expect("SAY", r"approach_on circuit_start", m3, timeout=4)
    v.wait_nav(lambda n: n["mode"].startswith("approach"), 5, "approach mode")
    m4 = v.mark()
    v.action("approach")
    v.expect("SAY", r"\[urgent\] approach_off$", m4, timeout=4)

    v.sim(speed=0, vz=-8, to=GROUND_M)
    v.expect("APP", r"Landed after", m4, timeout=40)
    for f in v.get_json("/api/logs")["files"]:
        if f["n"] not in before:
            v._open("/api/log?name=" + urllib.parse.quote(f["n"]), method="DELETE")
    return "refusals, emergency 6 o'clock + on course, approach start and cancel"


def t_battery(v, a):
    """Battery percentage curve, minutes left and the low-battery call."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    learned = None
    mins = []
    for volts, pct in ((4.20, 100), (4.09, 100), (4.00, 80), (3.80, 50), (3.60, 12), (3.30, 0)):
        v.sim(batt=volts)
        m = v.mark()
        _, mm = v.expect("BAT", rf"v={volts:.3f} pct=(\d+) min=(-?\d+) learned=(\d)", m, timeout=6)
        learned = mm.group(3) == "1"
        if not learned:
            near(mm.group(1), pct, 1, f"percentage at {volts} V")
        mins.append(int(mm.group(2)))
    if -1 in mins:
        mins_note = "minutes left unknown (real battery charging or absent)"
    elif any(b > a_ for a_, b in zip(mins, mins[1:])) or mins[0] <= 0 or (not learned and mins[-1] != 0):
        raise Fail(f"minutes left should fall to 0 with voltage: {mins}")
    else:
        mins_note = f"minutes {mins}"
    v.sim(batt=3.90)
    time.sleep(2.5)
    v.sim(batt=3.40)
    m = v.mark()
    v.never("SAY", r"battery_low", m, 6)
    v.expect("SAY", r"\[normal\] battery_low$", m, timeout=10, what="low battery called after 10 s")
    v.sim(batt="off")
    m = v.mark()
    _, mm = v.expect("BAT", r"v=(\d\.\d+)", m, timeout=6)
    return (f"curve ok{' (learned table in use, exact % not checked)' if learned else ''}, "
            f"{mins_note}, real battery back at {mm.group(1)} V")


def t_battery_learner(v, a):
    """Battery learner run on two simulated discharges (in RAM, nothing saved)."""
    m = v.mark()
    v.sim(learntest=1)
    _, mm = v.expect("BATL", r"selftest default410=([\d.]+) learned=(\d) m410=([\d.]+) m375=([\d.]+) "
                     r"left375=(-?\d+) m410b=([\d.]+) charging=(-?\d+)", m, timeout=15)
    d410, learned, m410, m375, left375, m410b, charging = mm.groups()
    near(d410, 360, 1, "minutes at 4.10 V before learning (runtime setting)")
    near(learned, 1, 0, "learned flag after a full discharge")
    near(m410, 234.2, 1.5, "learned minutes at 4.10 V after a 4 h discharge")
    near(m375, 117.5, 1.5, "learned minutes at 3.75 V")
    near(left375, 59, 3, "minutes left at 3.75 V in a discharge running twice as fast")
    near(m410b, 199.4, 1.5, "minutes at 4.10 V after blending in a 2 h discharge")
    near(charging, -1, 0, "minutes left while charging (unknown)")
    return f"4 h run -> {m410} min at 4.10 V, pace-scaled {left375} min, blended {m410b}, charging unknown"


def t_speech(v, a):
    """Test phrase: clip sequence correct and (with audio) heard by the microphone."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    v.set(speechVolume=max(a.volume, 40))  # well clear of room noise for the microphone check
    try:
        time.sleep(2)
        quiet = v.listen(4.0) if a.audio else None
        m = v.mark()
        v.action("test_voice")
        v.expect("SAY", r"\[urgent\] system_ready n5 hundred feet n3 oclock n1 point n3 kilometres$", m,
                 timeout=4)
        if not a.audio:
            return "clip sequence ok"
        bin_hz, db = v.listen(4.0)
        # Voiced speech shows as strong narrow peaks: compare the 20 loudest bins of the voice band.
        loud = band_top(bin_hz, db, 150, 950)
        base = band_top(quiet[0], quiet[1], 150, 950)
        if loud - base < 10:
            raise Fail(f"microphone did not hear speech: peaks {loud:.0f} dBFS against {base:.0f} when quiet "
                       "(needs a reasonably quiet room)")
        time.sleep(3)
        return f"clip sequence ok, speech peaks {loud - base:+.0f} dB over the quiet room"
    finally:
        v.set(speechVolume=a.volume)


# --------------------------------------------------------------------------------------------
# Web page tests: the real page in a headless browser, checked against simulated inputs.

def t_glide(v, a):
    """Glide ratio tile: ground speed over sink rate, short and long averages, none when level or climbing."""
    on_ground_with_fix(v)
    v.set(glideShortS=1, glideLongS=20)
    st = v.status()
    if "glide" not in st or st["glide"]:
        raise Fail(f"glide values reported on the ground: {st.get('glide')}")
    m = take_off(v, climb_to=GROUND_M + 80, vz=4)
    v.expect("SIM", r"reached target", m, timeout=40, what="climb to 80 m")
    time.sleep(3)
    if v.status()["glide"].get("now") is not None:
        raise Fail(f"glide ratio reported while level: {v.status()['glide']}")
    v.sim(speed=10, vz=-1.5, to=GROUND_M + 25)  # 10 m/s forward, 1.5 m/s down: 6.7 : 1
    v.wait_nav(lambda n: abs(float(n["vs"]) + 1.5) < 0.15, 12, "sinking at 1.5 m/s")
    time.sleep(2)
    g = v.status()["glide"]
    if "now" not in g:
        raise Fail(f"no glide ratio while descending: {g}")
    near(g["now"], 6.67, 1.0, "glide ratio over 1 s")
    time.sleep(25)  # the 20 s window now holds nothing but descent
    g = v.status()["glide"]
    if "avg" not in g:
        raise Fail(f"no long average after 25 s of steady descent: {g}")
    near(g["avg"], 6.67, 0.8, "glide ratio over 20 s")
    note = f"{g['now']:.1f} : 1 now, {g['avg']:.1f} : 1 over 20 s (true 6.7)"
    if a.ui:
        p = v.page()
        p.tab("flight")
        p.wait_text("t-glide", r"^6\.\d : 1$", 8, "glide ratio tile")
        p.wait_text("t-glides", r"^20 s avg 6\.\d : 1$", 8, "long average under the glide tile")
    v.sim(vz=1.5, to=GROUND_M + 60)
    v.wait_nav(lambda n: float(n["vs"]) > 1.2, 12, "climbing")
    time.sleep(3)
    if v.status()["glide"].get("now") is not None:
        raise Fail(f"glide ratio reported while climbing: {v.status()['glide']}")
    v.set(glideLongS=TEST_SETTINGS["glideLongS"])
    return note


def t_volume_ramp(v, a):
    """A volume change is applied gradually at the configured rate, never as a jump."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    v.set(autoVolume=False, volumeRampPctS=500, varioVolume=10, speechVolume=10)
    try:
        time.sleep(1.0)
        near(v.status()["vol"]["vario"], 10, 1, "volume before the change")
        v.set(volumeRampPctS=30)
        t0 = time.time()
        v.set(varioVolume=100)
        seen = []
        while time.time() - t0 < 4.6:
            seen.append((time.time() - t0, v.status()["vol"]["vario"]))
            time.sleep(0.2)
        # 30 %/s from 10 %: about 40 % after a second, full after three.
        early = max(x for t, x in seen if t < 1.0)
        mid = [x for t, x in seen if 1.2 < t < 1.8]
        if early > 46:
            raise Fail(f"volume jumped: {early} % within a second of raising the setting (limit 30 %/s)")
        if not mid or not 25 <= mid[0] <= 70:
            raise Fail(f"volume about 1.5 s into the ramp is {mid}, expected 35 to 65 %")
        if any(b < a_ for (_, a_), (_, b) in zip(seen, seen[1:])):
            raise Fail(f"volume did not rise steadily: {[x for _, x in seen]}")
        near(seen[-1][1], 100, 0, "volume at the end of the ramp")
        v.set(varioVolume=10)
        time.sleep(1.0)
        down = v.status()["vol"]["vario"]
        if not 50 <= down <= 95:
            raise Fail(f"volume a second after lowering the setting is {down} %, expected a gradual fall")
        return f"10 -> 100 %: {early} % within 1 s, {mid[0]} % at 1.5 s, 100 % by 4.5 s; falls gradually too"
    finally:
        v.set(volumeRampPctS=TEST_SETTINGS["volumeRampPctS"], varioVolume=a.volume, speechVolume=a.volume)


def schema_keys(v):
    return {f["k"] for f in v.get_json("/api/schema")}


def t_variant_no_gps(v, a):
    """Vario without a GPS (switched off in the test build): ready on the barometer alone, no
    GPS announcements or guidance, takeoff and landing still detected, settings and page trimmed."""
    logs_before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    reset(v, baro="on", alt=GROUND_M, vz=0, gps="silent")
    v.set(landingTimeS=15)  # (30 s of steady height without a GPS) so the page checks finish while still flying
    try:
        m = v.mark()
        v.sim(hasGps=0, reset=1)
        # A good fix is simulated from here on: with the GPS "not fitted" it must be ignored.
        v.sim(gps="fix", sats=10, hdop=0.9, lat=51.5, lon=0.5, speed=0)
        v.expect("RDY", r"^ready$", m, timeout=25, what="ready to fly without a GPS")
        st = v.status()
        if st["hw"]["gps"] or "gps" in st or not st["ready"]:
            raise Fail(f"status without GPS: hw {st['hw']}, gps section {'gps' in st}, ready {st['ready']}")
        keys = schema_keys(v)
        extra = keys & {"readyMinSats", "lzMode", "glideRatio", "igcEnabled", "takeoffSpeedMs", "windManual",
                        "bleGps", "distUnit"}
        missing = {"baroTakeoffGainM", "varioVolume", "landingTimeS", "heightUnit", "bleRateHz"} - keys
        if extra or missing:
            raise Fail(f"settings without GPS: still listed {sorted(extra)}, wrongly removed {sorted(missing)}")
        v.action("emergency")
        v.action("approach")
        v.never("SAY", r"no_gps|emergency|approach|wind_unknown", m, 2.0)

        v.sim(vz=2, to=GROUND_M + 40)
        v.expect("FLT", r"^takeoff \(baro only\)", m, timeout=14)
        v.expect("SAY", r"\[high\] takeoff$", m, timeout=4)
        v.expect("SIM", r"reached target", m, timeout=30)
        n = v.wait_nav(lambda n: 36 < float(n["agl"]) < 43 and n["fly"] == "1", 8, "height above takeoff")
        if n["fix"] != "0":
            raise Fail("the (simulated) GPS fix was used although the GPS is not fitted")
        if a.ui:
            p = v.page()
            p.tab("flight")
            p.wait("document.body.classList.contains('nogps')", 6, "page told there is no GPS")
            hidden = p.js("['t-gps','t-speed','t-lz','t-wind','b-approach','b-emergency']"
                          ".filter(i=>document.getElementById(i).offsetParent!==null)")
            if hidden:
                raise Fail(f"web page: still shown without a GPS: {hidden}")
            p.wait_text("t-flight", r"^\d+:\d\d$", 6, "elapsed flight time while flying")
            p.wait_text("t-ftime", r"^Flying$", 6)
            p.wait_text("t-agl", r"^1[23]\d ft$", 6, "height shown without a GPS")
            check_tile_layout(p, gps=False, mic=True)
            check_height_graph(p)
            p.check_fits("flight tab without GPS")
            p.tab("settings")
            groups = p.js("[...document.querySelectorAll('#settings summary')].map(s=>s.firstChild.textContent)")
            if "GPS" in groups or "Landing zone" in groups or "Vario" not in groups:
                raise Fail(f"web page: settings groups without a GPS: {groups}")
        v.expect("APP", r"Landed after", m, timeout=50, what="landing from a steady vario")
        v.never("SAY", r"gps_fix|no_gps", m, 0.5)
        new = [f["n"] for f in v.get_json("/api/logs")["files"] if f["n"] not in logs_before]
        if new:
            raise Fail(f"a flight log was created without a GPS: {new}")
        if a.ui:
            p.tab("flight")
            p.wait_text("ready-t", r"^Ready to fly$", 20, "ready banner without a GPS")
            if p.text("ready-s") != "":
                raise Fail("web page: satellite count shown without a GPS")
        return f"ready, takeoff, {n['agl']} m and landing on the barometer; {len(keys)} settings listed, no log"
    finally:
        v.sim(hasGps=1)
        v.set(landingTimeS=TEST_SETTINGS["landingTimeS"])


def t_variant_no_mic(v, a):
    """Vario without a microphone (switched off in the test build): set volume is used whatever
    the noise, battery still read, no spectrum, settings and page trimmed."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    v.set(autoVolume=True, noiseQuietDb=-50, noiseLoudDb=-30, speechVolume=20, varioVolume=40)
    try:
        batt = v.status()["batt"]["v"]
        v.sim(noise=-20)
        end = time.time() + 6
        while v.status()["vol"]["vario"] != 100:
            if time.time() > end:
                raise Fail("volume did not rise with noise while the microphone was still on")
            time.sleep(0.3)
        m = v.mark()
        v.sim(hasMic=0)
        end = time.time() + 6
        while True:
            st = v.status()
            if (st["vol"]["vario"], st["vol"]["speech"]) == (40, 20):
                break
            if time.time() > end:
                raise Fail(f"volume without a microphone is {st['vol']}, expected the set 40 / 20 %")
            time.sleep(0.3)
        if st["hw"]["mic"] or "mic" in st:
            raise Fail(f"status without microphone: hw {st['hw']}, mic section {'mic' in st}")
        v.expect("VOL", r"auto off: speech 20% \(set 20%, \+0\), vario 40%", m, timeout=8)
        code, _ = v._open("/api/spectrum")
        if code != 204:
            raise Fail(f"spectrum without a microphone: HTTP {code}, expected 204 (nothing)")
        keys = schema_keys(v)
        extra = keys & {"autoVolume", "noiseQuietDb", "noiseLoudDb", "noiseReleaseS", "rpmMin", "rpmPulsesPerRev"}
        missing = {"speechVolume", "varioVolume", "volumeRampPctS", "readyMinSats"} - keys
        if extra or missing:
            raise Fail(f"settings without microphone: still listed {sorted(extra)}, wrongly removed {sorted(missing)}")
        time.sleep(2.5)
        near(v.status()["batt"]["v"], batt, 0.08, "battery voltage still read without the microphone")
        if a.ui:
            p = v.page()
            p.tab("flight")
            p.wait("document.body.classList.contains('nomic')", 6, "page told there is no microphone")
            shown = p.js("['t-noise','t-rpm'].filter(i=>document.getElementById(i).offsetParent!==null)")
            if shown or p.js("document.querySelector('button[data-tab=spectrum]').offsetParent!==null"):
                raise Fail(f"web page: still shown without a microphone: {shown or 'Spectrum tab'}")
            p.wait_text("t-batt", r"^(\d+%|USB)$", 6)
            check_tile_layout(p, gps=True, mic=False)
            p.check_fits("flight tab without microphone")
        return f"volume back to the set 40/20 % at -20 dBFS noise, {len(keys)} settings listed, no spectrum"
    finally:
        v.sim(hasMic=1, noise="off")
        v.set(autoVolume=False, speechVolume=a.volume, varioVolume=a.volume)


def check_tile_layout(p, gps, mic):
    """Flight tab: tiles in their groups, graph beside Height, a lone last tile full width."""
    info = p.js("""(()=>{const vis=e=>getComputedStyle(e).display!=='none';
      const ids=s=>[...document.getElementById(s).children].filter(vis).map(e=>e.querySelector('.v')?e.querySelector('.v').id:'graph');
      const g=document.getElementById('tile-hgraph');
      return {main:ids('tilesMain'),more:ids('tilesMore'),
        graphAfter:g.previousElementSibling.id,
        heightBig:document.getElementById('tile-height').classList.contains('big'),
        wide:[...document.querySelectorAll('#tilesMore .wide')].map(e=>e.querySelector('.v').id),
        graphPixels:(()=>{const c=document.getElementById('hgCv');if(!c.width)return -1;
          const d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;let n=0;for(let i=3;i<d.length;i+=4)if(d[i])n++;return n})(),
        graphW:document.getElementById('hgCv').clientWidth,tileW:document.getElementById('tile-height').clientWidth}})()""")
    want_main = ["t-agl", "graph", "t-vario"] + (["t-speed", "t-glide"] if gps else []) + ["t-flight"] + \
                (["t-lz", "t-wind"] if gps else [])
    if info["main"] != want_main:
        raise Fail(f"web page: top tiles are {info['main']}, expected {want_main}")
    want_more = (["t-gps"] if gps else []) + ["t-batt"] + (["t-rpm", "t-noise"] if mic else [])
    if info["more"] != want_more:
        raise Fail(f"web page: lower tile group is {info['more']}, expected {want_more}")
    if info["heightBig"] or info["graphAfter"] != "tile-height":
        raise Fail(f"web page: with an even number of cells the graph belongs beside Height (big={info['heightBig']}, "
                   f"graph follows {info['graphAfter']})")
    if len(want_more) % 2 == 1 and info["wide"] != [want_more[-1]]:
        raise Fail(f"web page: lone last tile {want_more[-1]} not full width (wide: {info['wide']})")
    if len(want_more) % 2 == 0 and info["wide"]:
        raise Fail(f"web page: tiles wrongly full width: {info['wide']}")
    if abs(info["graphW"] - info["tileW"]) > 40:
        raise Fail(f"web page: graph {info['graphW']} px wide beside a Height tile {info['tileW']} px wide")
    return info


def check_height_graph(p):
    """The graph has been fed from /api/height and has drawn something."""
    p.wait("hgData&&hgData.a.length>=2", 10, "height history fetched for the graph")
    p.wait("(()=>{const c=document.getElementById('hgCv');if(!c.width)return false;"
           "const d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;"
           "for(let i=3;i<d.length;i+=4)if(d[i])return true;return false})()", 6, "height graph drawn")


def docs_text(name):
    return open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "docs", name), encoding="utf-8").read()


DOC_FILES = ("Feature_Reference.md", "User_Guide_(basic).md", "User_Guide_(advanced).md")


def stub_docs(p, mode="local"):
    """Makes the page's fetches of the guides from GitHub's raw file server answer from the local docs
    folder (so the test needs no internet and checks the documents as they are in this working copy),
    or, with mode="offline", fail the way a phone without internet would."""
    docs = {n: docs_text(n) for n in DOC_FILES}
    p.js("(()=>{window.__docs=%s;window.__fetched=[];const rf=window.fetch.bind(window);"
         "window.fetch=(u,o)=>{if(typeof u==='string'&&u.startsWith(HELPRAW)){"
         "const n=decodeURIComponent(u.slice(HELPRAW.length));window.__fetched.push(n);"
         "if(%s)return Promise.reject(new TypeError('offline'));"
         "return Promise.resolve(n in window.__docs?new Response(window.__docs[n],{status:200}):new Response('',{status:404}))}"
         "return rf(u,o)}})()" % (json.dumps(docs), "true" if mode == "offline" else "false"))


def t_ui_help(v, a):
    """Help tab and the Help links on the settings groups: the right guide for the hardware, shown inside
    the page as plain content (rendered from the Markdown), links that lead to sections that exist."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    ref = docs_text("Feature_Reference.md")
    guide_adv = docs_text("User_Guide_(advanced).md")
    guide_basic = docs_text("User_Guide_(basic).md")
    p = v.page()
    p.wait("document.querySelectorAll('#helpBody a.doc').length>=2", 8, "help links built")
    web = p.js("HELPWEB")
    raw = p.js("HELPRAW")
    if not re.match(r"^https://github\.com/[\w.-]+/[\w.-]+/blob/main/docs/$", web) or \
            not re.match(r"^https://raw\.githubusercontent\.com/[\w.-]+/[\w.-]+/main/docs/$", raw):
        raise Fail(f"web page: help addresses are {web} and {raw}")
    heads = [re.sub(r"[^a-z0-9 -]", "", h.lower()).replace(" ", "-") for h in re.findall(r"^#+ (.*)$", ref, re.M)]
    ids = set(re.findall(r'<a id="([\w-]+)"></a>', ref)) | set(heads)

    def tab_links():
        return p.js("[...document.querySelectorAll('#helpBody a')].map(a=>({t:a.firstChild.textContent,h:a.href}))")

    def check_anchors(links):
        for l in links:
            if not l["h"].startswith(web):
                raise Fail(f"web page: help link {l['t']} leaves the docs folder: {l['h']}")
            anchor = l["h"].partition("#")[2]
            if "Feature_Reference.md" in l["h"] and anchor and anchor not in ids:
                raise Fail(f"Feature_Reference.md has no section {anchor} (link {l['t']})")

    p.tab("help")
    links = tab_links()
    if links[0]["t"] != "User guide" or "User_Guide_%28advanced%29.md" not in links[0]["h"]:
        raise Fail(f"web page: first help link on a GPS + microphone vario is {links[0]}")
    if not any(l["h"].endswith("Feature_Reference.md") for l in links):
        raise Fail("web page: no link to the feature reference")
    check_anchors(links)
    p.check_fits("help tab")
    if p.js("!!document.getElementById('helpIntro')"):
        raise Fail("web page: the Help tab still has an introduction paragraph")
    words = p.js("document.getElementById('helpPage').textContent")
    if "Basic" in words or "Advanced" in words:
        raise Fail(f"web page: the Help tab talks about versions: {words[:120]}")

    p.tab("settings")
    p.wait("document.querySelectorAll('#settings summary .help a').length>0", 8, "group help links")
    groups = p.js("[...document.querySelectorAll('#settings summary')].map(s=>({g:s.firstChild.textContent,"
                  "l:[...s.querySelectorAll('.help a')].map(a=>({t:a.textContent,h:a.href}))}))")
    bare = [g["g"] for g in groups if not g["l"] and g["g"] != "Advanced settings"]
    if bare:
        raise Fail(f"web page: settings groups with no help link: {bare}")
    for g in groups:
        check_anchors([{"t": g["g"] + " / " + l["t"], "h": l["h"]} for l in g["l"]])
    shared = {g["g"]: [l["t"] for l in g["l"]] for g in groups}
    if shared["Audio"] != ["Help", "More help"] or shared["Vario"] != ["Help"]:
        raise Fail(f"web page: help link names: Audio {shared['Audio']}, Vario {shared['Vario']}")

    # the group Help links appear only while their group is open
    p.js("[...document.querySelectorAll('#settings details')].forEach(d=>d.open=false)")
    shown = p.js("[...document.querySelectorAll('#settings summary .help a')].filter(a=>a.offsetParent!==null).length")
    if shown:
        raise Fail(f"web page: {shown} group Help links visible although every group is closed")
    p.js("[...document.querySelectorAll('#settings summary')].find(s=>s.firstChild.textContent==='Vario').parentElement.open=true")
    seen = p.js("[...document.querySelectorAll('#settings details')].filter(d=>d.querySelector('summary .help a')"
                "&&d.querySelector('summary .help a').offsetParent!==null).map(d=>d.querySelector('summary').firstChild.textContent)")
    if seen != ["Vario"]:
        raise Fail(f"web page: Help links visible for {seen} with only the Vario group open")

    # ---- the reader: content only, rendered from the Markdown of the guides
    stub_docs(p)
    p.js("[...document.querySelectorAll('#settings summary')].find(s=>s.firstChild.textContent==='Vario')"
         ".querySelector('.help a').click()")
    p.wait("!document.getElementById('helpReader').hidden&&!document.getElementById('helpPage').hidden", 6, "reader opens")
    p.wait("document.querySelectorAll('#helpDoc h3').length>5", 6, "reference text rendered")
    if p.js("document.getElementById('helpPage').hidden"):
        raise Fail("web page: the settings help link did not switch to the Help tab")
    fetched = p.js("window.__fetched")
    if fetched != ["Feature_Reference.md"]:
        raise Fail(f"web page: the Vario help link fetched {fetched}")
    # nothing from GitHub's page around the guide, and no Markdown left over in the text
    text = p.js("document.getElementById('helpDoc').textContent")
    for junk in ("**", "](", "```", "|---", "<a id", "&lt;"):
        if junk in text:
            raise Fail(f"web page: Markdown left in the rendered guide: {junk!r}")
    if p.js("!!document.getElementById('helpWeb')"):
        raise Fail("web page: the reader still has an Open on GitHub link")
    if p.js("document.querySelectorAll('#helpDoc iframe,#helpDoc script').length"):
        raise Fail("web page: unexpected elements in the rendered guide")
    top = p.js("document.getElementById('hd-basic-settings-vario').getBoundingClientRect().top")
    if not -5 <= top <= 400:
        raise Fail(f"web page: the Vario section is not at the top of the screen after opening (top {top:.0f} px)")
    rendered = set(p.js("[...document.querySelectorAll('#helpDoc [id^=hd-]')].map(e=>e.id.slice(3))"))
    if not ids <= rendered:
        raise Fail(f"web page: sections missing from the rendered reference: {sorted(ids - rendered)[:6]}")
    tables = len(re.findall(r"^\|[\s:|-]+\|\s*$", ref, re.M))
    got = p.js("document.querySelectorAll('#helpDoc table').length")
    if got != tables:
        raise Fail(f"web page: {got} tables rendered, the reference has {tables}")
    if p.js("document.documentElement.scrollWidth>window.innerWidth+1"):
        raise Fail("web page: the rendered reference is wider than a phone screen")
    pres = len(re.findall(r"^```", ref, re.M)) // 2
    if p.js("document.querySelectorAll('#helpDoc pre').length") != pres:
        raise Fail("web page: code blocks of the reference not rendered")

    # a link between guides opens the other guide in the same reader; Back returns
    p.js("document.getElementById('helpBack').click()")
    p.wait("document.getElementById('helpReader').hidden&&!document.getElementById('helpList').hidden", 4, "Back shows the list")
    p.js("[...document.querySelectorAll('#helpBody a.doc')][0].click()")
    p.wait("document.querySelector('#helpDoc h1')&&document.querySelector('#helpDoc h1').textContent.includes('user guide')", 6,
           "user guide rendered")
    p.js("document.querySelector('#helpDoc a[href*=\"Feature_Reference.md#\"]').click()")
    p.wait("document.querySelector('#helpDoc h1')&&document.querySelector('#helpDoc h1').textContent.includes('feature reference')", 6,
           "a link inside the guide opens the reference in the reader")
    p.js("document.getElementById('helpBack').click()")
    p.wait("document.querySelector('#helpDoc h1').textContent.includes('user guide')", 6, "Back returns to the guide")
    # an in-page link (the contents list of the reference) scrolls inside the reader
    p.js("[...document.querySelectorAll('#helpDoc a')].find(a=>a.getAttribute('href')==='#settings-units-and-audio')||0")

    # without internet the reader says so and offers the GitHub address
    stub_docs(p, "offline")
    p.js("document.getElementById('helpBack').click();document.getElementById('helpBack').click()")
    p.js("[...document.querySelectorAll('#helpBody a.doc')][1].click()")
    p.wait("document.getElementById('helpDoc').textContent.includes('Could not load')", 6, "message when offline")
    if "internet" not in p.js("document.getElementById('helpDoc').textContent"):
        raise Fail("web page: the message for a guide that cannot be loaded does not mention internet access")
    p.js("document.getElementById('helpBack').click()")

    # the guides themselves: every relative link in them points at a document or section that exists
    for name, text in (("User_Guide_(advanced).md", guide_adv), ("User_Guide_(basic).md", guide_basic)):
        for t, target in re.findall(r"\[([^\]]+)\]\(((?:[^()\s]|\([^()]*\))+)\)", text):
            if target.startswith("http"):
                continue
            f, _, anchor = target.partition("#")
            if f and f not in DOC_FILES:
                raise Fail(f"{name}: link {t} points at {f}, which is not a guide")
            if f == "Feature_Reference.md" and anchor and anchor not in ids:
                raise Fail(f"{name}: link {t} points at section {anchor}, which the reference lacks")
    note = f"{len(links)} links on the Help tab, {len(groups)} settings groups linked, guides rendered in the page"

    # With neither a GPS nor a microphone the guide for that vario is offered.
    try:
        v.sim(hasGps=0, hasMic=0)
        p = v.page()
        p.wait("document.body.classList.contains('nogps')&&document.body.classList.contains('nomic')", 8,
               "page told there is neither GPS nor microphone")
        p.tab("help")
        links = tab_links()
        if links[0]["t"] != "User guide" or "User_Guide_%28basic%29.md" not in links[0]["h"]:
            raise Fail(f"web page: first help link without GPS and microphone is {links[0]}")
        if any("Basic" in l["t"] or "Advanced" in l["t"] for l in links):
            raise Fail(f"web page: the Help tab talks about versions: {[l['t'] for l in links]}")
        if any("adv-" in l["h"] for l in links):
            raise Fail("web page: advanced section links offered without GPS and microphone")
        p.tab("settings")
        adv_links = p.js("[...document.querySelectorAll('#settings summary .help a')].filter(a=>a.href.includes('#adv-')).length")
        if adv_links:
            raise Fail(f"web page: {adv_links} advanced help links on the settings page without GPS and microphone")
        stub_docs(p)
        p.tab("help")
        p.js("[...document.querySelectorAll('#helpBody a.doc')][0].click()")
        p.wait("document.querySelector('#helpDoc h1')&&document.querySelector('#helpDoc h1').textContent.includes('Basic vario')", 6,
               "basic guide shown without GPS and microphone")
        note += "; the guide for a vario without GPS and microphone is offered then"
    finally:
        v.sim(hasGps=1, hasMic=1)
    return note


def t_ui_flight(v, a):
    """Flight tab: banner and every tile show the simulated situation, and follow changes."""
    m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="fix", sats=10, hdop=0.9,
              speed=0, track=90, lat=51.5, lon=0.5, batt=4.00, noise=-60)
    v.expect("RDY", r"^ready$", m, timeout=25)
    st = v.status()
    p = v.page()
    p.tab("flight")
    p.wait_text("t-gps", r"^10 sats$", 8)
    p.wait_text("ready-t", r"^Ready to fly$", 8)
    if p.js("document.getElementById('ready').className") != "ready ok":
        raise Fail("web page: ready banner is not green")
    p.wait_text("ready-s", r"^10 / 6 satellites$")
    agl = p.wait_text("t-agl", r"^-?[0-7] ft$", 20, "Height AGL tile about 0 ft on the ground")
    near(p.wait_text("t-msl", r"^\d+ ft MSL$").split()[0], 328, 8, "MSL under the height tile (ft)")
    p.wait_text("t-vario", r"^[+-]0\.[01] m/s$")
    p.wait_text("t-speed", r"^0 km/h$")
    p.wait_text("t-track", r"^track \d+°$")
    p.wait_text("t-flight", r"^Grounded$")
    near(p.wait_text("t-lz", r"^\d+ m$", what="distance to the landing zone").split()[0], 0, 6, "LZ distance")
    p.wait_text("t-lzs", r"ft MSL$")
    p.wait_text("t-wind", r"^unknown$")
    p.wait_text("t-gpss", r"^Fix · signal strong$")
    p.wait_text("t-batts", r"^4\.00 V")
    if not st["batt"]["learned"]:
        p.wait_text("t-batt", r"^80%$")
    p.wait_text("t-noise", r"^-60 dBFS$")
    p.wait_text("t-noises", rf"^vario vol {a.volume}%$")
    p.wait_text("t-rpm", r"^(off|\d+ rpm)$")
    p.wait_text("hver", rf"^v{re.escape(st['version'])} debug$")
    check_height_graph(p)
    p.check_fits("flight tab")
    p.wait_text("t-glide", r"^–$")
    p.wait_text("t-glides", r"^20 s avg –$")
    check_tile_layout(p, gps=True, mic=True)
    # Full screen button: shown, labelled, and asks the browser for full screen when tapped
    if p.js("document.getElementById('b-fs').offsetParent===null") or p.text("b-fs") != "Full screen":
        raise Fail(f"web page: Full screen button missing or mislabelled ({p.text('b-fs')!r})")
    p.js("window.__fs=0;document.documentElement.requestFullscreen=()=>{window.__fs++;return Promise.resolve()}")
    p.click("#b-fs")
    if p.js("window.__fs") != 1:
        raise Fail("web page: the Full screen button did not ask the browser for full screen")

    v.sim(speed=12)
    p.wait_text("t-speed", r"^43 km/h$", 8, "12 m/s shown in km/h")
    v.sim(speed=0, vz=1.5)  # not both at once: speed plus climb would be a takeoff
    p.wait_text("t-vario", r"^\+1\.[3-6] m/s$", 20)
    v.sim(vz=0, batt=3.30)
    p.wait_text("t-batt", r"^0%$")
    v.sim(gps="silent")
    p.wait_text("t-gps", r"^no data$")
    p.wait_text("ready-t", r"^GPS not responding$")
    p.wait_text("t-speed", r"^–$", 8, "no speed without a fix")
    if p.js("document.getElementById('ready').className") != "ready wait":
        raise Fail("web page: banner should be amber while not ready")
    v.sim(gps="fix", sats=4, hdop=4.2)
    p.wait_text("t-gps", r"^4 sats$", 8)
    p.wait_text("t-gpss", r"^Fix · signal good$")
    p.wait_text("ready-t", r"^Waiting for satellites$")
    p.wait_text("ready-s", r"^4 / 6 satellites$")
    p.wait_text("t-speed", r"^0 km/h$", 8, "weak-fix speed shown as 0")
    return "banner, 11 tiles, height graph and layout correct; speed, vario, battery and GPS changes followed"


def t_ui_actions(v, a):
    """Action buttons: mute, test voice and the two landing-zone buttons."""
    m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="nofix")
    p = v.page()
    try:
        return ui_actions_body(v, a, p, m)
    finally:
        v.set(lzMode=0)


def ui_actions_body(v, a, p, m):
    p.tab("flight")
    p.click("#b-mute")
    v.expect("SAY", r"\[urgent\] vario_muted$", m, timeout=4, what="Mute button")
    p.wait_text("b-mute", r"^Unmute vario$")
    p.wait_text("t-mute", r"^muted$")
    p.wait_text("toast", r"^Done$")
    m2 = v.mark()
    p.click("#b-mute")
    v.expect("SAY", r"\[urgent\] vario_on$", m2, timeout=4, what="Unmute button")
    p.wait_text("b-mute", r"^Mute vario$")
    p.click('button[data-a="test_voice"]')
    v.expect("SAY", r"\[urgent\] system_ready ", m2, timeout=4, what="Test voice button")
    time.sleep(4)
    p.click('button[data-a="lz_here"]')
    p.wait_text("toast", r"^Failed: no GPS fix$", 5, "Set LZ here refused without a fix")

    v.sim(gps="fix", sats=10, hdop=0.9, speed=0, lat=51.5, lon=0.5)
    v.expect("RDY", r"^ready$", m2, timeout=25)
    m3 = v.mark()
    p.click('button[data-a="lz_here"]')
    v.expect("SAY", r"\] lz_set$", m3, timeout=4, what="Set LZ here button")
    s = v.settings()
    near(s["lzMode"], 1, 0, "LZ mode after Set LZ here (manual)")
    near(s["lzLat"], 51.5, 0.001, "saved LZ latitude")
    p.tab("settings")
    p.wait("tInput('lzMode') && tInput('lzMode').value==='1'", 12, "settings tab shows the manual LZ")
    p.tab("flight")
    p.click('button[data-a="lz_takeoff"]')
    end = time.time() + 6
    while v.settings()["lzMode"] != 0:
        if time.time() > end:
            raise Fail("web page: LZ = takeoff button did not switch the LZ mode back")
        time.sleep(0.5)
    return "mute, test voice, Set LZ here (refused and accepted), LZ = takeoff"


def t_ui_guidance(v, a):
    """In flight: flying tiles, emergency and approach banners, then the log list and its Delete."""
    on_ground_with_fix(v)
    logs_before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    p = v.page()
    p.tab("flight")
    m = v.mark()
    take_off(v, climb_to=GROUND_M + 100, vz=4)
    p.wait_text("t-flight", r"^\d+:\d\d$", 8, "elapsed flight time while flying")
    p.wait_text("t-ftime", r"^Flying · logging$", 8)
    p.wait("document.getElementById('ready').className==='ready hide'", 5, "ready banner hidden in flight")
    p.wait_text("t-wind", r"^\d+°$", 8, "wind from the takeoff run")
    p.wait_text("t-winds", r"takeoff$")
    v.expect("SIM", r"reached target", m, timeout=40)

    m2 = v.mark()
    p.click("#b-emergency")
    v.expect("SAY", r"\[urgent\] emergency glide_range", m2, timeout=4, what="Emergency button")
    p.wait("document.getElementById('ready').className==='ready emerg'", 5, "red emergency banner")
    p.wait_text("ready-t", r"^Emergency$")
    p.wait_text("ready-s", r"^range [\d.]+ (m|km)$")
    p.wait("document.getElementById('b-emergency').classList.contains('on')", 3, "Emergency button lit")
    p.click("#b-emergency")
    p.wait("document.getElementById('ready').className==='ready hide'", 5, "banner hidden after cancel")
    time.sleep(1.5)
    m3 = v.mark()
    p.click("#b-approach")
    v.expect("SAY", r"approach_on", m3, timeout=4, what="Approach button")
    p.wait("document.getElementById('ready').className==='ready appr'", 5, "blue approach banner")
    p.wait_text("ready-t", r"^Approach$")
    p.wait_text("ready-s", r"\S")
    p.click("#b-approach")
    p.wait("document.getElementById('ready').className==='ready hide'", 5, "banner hidden after cancel")
    p.check_fits("flight tab in flight")

    v.sim(speed=0, vz=-8, to=GROUND_M)
    p.wait_text("t-flight", r"^Grounded$", 20)
    p.wait_text("t-ftime", r"^last flight \d+:\d\d$", 6, "length of the last flight after landing")
    # Only the log this test created is touched: the vario may hold real flight logs.
    fresh = [f["n"] for f in v.get_json("/api/logs")["files"] if f["n"] not in logs_before]
    if len(fresh) != 1 or not re.match(r"^210101-\d{6}\.igc$", fresh[0]):
        raise Fail(f"expected exactly one new flight log (210101-hhmmss.igc), the vario has {fresh}")
    name = fresh[0]
    row = f"[...document.querySelectorAll('#logs .log')].find(r=>r.querySelector('a').textContent==={json.dumps(name)})"
    for attempt in range(3):  # the list is fetched once per visit to the tab
        p.tab("flight")
        p.tab("system")
        try:
            p.wait(f"!!{row}", 5, "the new flight log listed")
            break
        except Fail:
            if attempt == 2:
                shown = p.js("document.getElementById('logs').textContent")
                raise Fail(f"web page: the new flight log {name} is not listed; the list shows '{shown[:80]}'")
    href = p.js(f"{row}.querySelector('a').getAttribute('href')")
    if href != "/api/log?name=" + name:
        raise Fail(f"web page: log download link is {href}")
    if not re.search(r"^\d+ kB$", p.js(f"{row}.querySelector('span').textContent")):
        raise Fail("web page: log size not shown")
    if not re.search(r"^\d+ kB free of 1024 kB$", p.js("document.querySelector('#logs p').textContent")):
        raise Fail("web page: free space line wrong")
    p.js(f"{row}.querySelector('button').click()")  # Delete (the confirm box is auto-accepted)
    p.wait(f"!{row}", 6, "log removed from the list after Delete")
    if any(f["n"] == name for f in v.get_json("/api/logs")["files"]):
        raise Fail("web page: Delete did not remove the file from the vario")
    return f"flying tiles, emergency and approach banners, {name} listed and deleted from the page"


def t_ui_settings(v, a):
    """Settings tab: every setting shown, edits saved, unit conversion, gating, restart marks."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    schema = v.get_json("/api/schema")
    p = v.page()
    p.tab("settings")
    try:
        groups = []
        for f in schema:
            if f["g"] not in groups:
                groups.append(f["g"])
        # Battery, Wi-Fi, GPS, Engine RPM, Wind, Logging and Glide sit in a collapsed "Advanced settings"
        # section at the end; the other groups keep the firmware's order.
        adv_order = ["Battery", "Wi-Fi", "GPS", "Engine RPM", "Wind", "Logging", "Glide"]
        main_groups = [g for g in groups if g not in adv_order]
        adv_groups = [g for g in adv_order if g in groups]
        shown = p.js("[...document.querySelectorAll('#settings>details>summary')].map(s=>s.firstChild.textContent)")
        want = main_groups + (["Advanced settings"] if adv_groups else [])
        if shown != want:
            raise Fail(f"web page: top-level setting groups {shown}, expected {want}")
        shown_adv = p.js("[...document.querySelectorAll('#advSettings>details>summary')].map(s=>s.firstChild.textContent)")
        if shown_adv != adv_groups:
            raise Fail(f"web page: Advanced settings holds {shown_adv}, expected {adv_groups}")
        if p.js("document.getElementById('advSettings').open"):
            raise Fail("web page: Advanced settings should start closed")
        rows = p.js("document.querySelectorAll('#settings label.row').length")
        near(rows, len(schema), 0, "number of settings shown")
        missing = p.js("schema.filter(f=>!tInput(f.k)).map(f=>f.k)")
        if missing:
            raise Fail(f"web page: no input found for {missing}")
        p.wait_text("w-ap", r"^ParaVario-[0-9A-F]{4} · 192\.168\.4\.1 · \d+ connected$", 4)
        p.wait_text("w-server", r"\S")

        # Number, saved as typed.
        p.js("tSet('climbThreshold','0.45')")
        p.wait_text("saveState", r"^Saved", 5, "edit saved")
        near(v.settings()["climbThreshold"], 0.45, 0.001, "climb threshold saved from the page")
        # Height value: shown in feet, stored in metres.
        near(p.js("tInput('takeoffGainM').value"), 16, 0, "takeoff gain of 5 m shown in feet")
        if "(ft)" not in p.js("tRow('takeoffGainM').textContent"):
            raise Fail("web page: unit label (ft) missing")
        p.js("tSet('takeoffGainM','20')")
        p.wait("document.getElementById('saveState').textContent==='Unsaved changes'", 2, "unsaved marker")
        p.wait_text("saveState", r"^Saved", 5)
        near(v.settings()["takeoffGainM"], 6.096, 0.01, "20 ft stored as metres")
        # Boundary list in feet, greyed out by its Announce checkbox.
        shown_list = p.js("tInput('boundariesUp').value")
        if shown_list != "50, 100":
            raise Fail(f"web page: boundaries shown as '{shown_list}', expected '50, 100'")
        p.js("tSet('boundaryClimbing',false)")
        p.wait("tInput('boundariesUp').disabled", 2, "boundaries greyed out when Announce is off")
        p.wait_text("saveState", r"^Saved", 5)
        if v.settings()["boundaryClimbing"]:
            raise Fail("web page: Announce checkbox not saved")
        p.js("tSet('boundaryClimbing',true)")
        p.wait("!tInput('boundariesUp').disabled", 2, "boundaries enabled again")
        p.js("tSet('boundariesUp','60, 120, 30')")
        p.wait_text("saveState", r"^Saved", 5)
        if v.settings()["boundariesUp"] != "9.14,18.29,36.58":
            raise Fail(f"web page: boundary list saved as {v.settings()['boundariesUp']}")
        # Changing the height unit redraws everything in metres.
        p.js("tSet('heightUnit','1')")
        p.wait("tInput('takeoffGainM') && tInput('takeoffGainM').value==='6'", 4, "values redrawn in metres")
        if "(m)" not in p.js("tRow('takeoffGainM').textContent"):
            raise Fail("web page: unit label (m) missing after unit change")
        p.wait_text("saveState", r"^Saved", 5)
        p.tab("flight")
        p.wait_text("t-agl", r"^-?\d+ m$", 5, "height tile in metres")
        p.tab("settings")
        p.js("tSet('heightUnit','0')")
        # Text.
        p.js("tSet('pilotName','Test Pilot')")
        p.wait_text("saveState", r"^Saved", 5)
        if v.settings()["pilotName"] != "Test Pilot":
            raise Fail("web page: pilot name not saved")
        # Restart mark, password boxes masked.
        if "· restart" not in p.js("tRow('gpsLed').textContent"):
            raise Fail("web page: restart mark missing on the GPS LED setting")
        if p.js("tInput('wifiPass').type") != "password" or p.js("tInput('serverPass').type") != "password":
            raise Fail("web page: Wi-Fi passwords are not masked")
        p.js("document.querySelectorAll('#settings details').forEach(d=>d.open=true)")
        p.check_fits("settings tab with every group open")
    finally:
        v.set(climbThreshold=0.3, takeoffGainM=5.0, heightUnit=0, boundaryClimbing=True,
              boundariesUp="15.24,30.48", pilotName="")
    return f"{len(groups)} groups, {len(schema)} settings; number, feet, list, checkbox gate, unit switch, text"


def t_ui_spectrum(v, a):
    """Spectrum tab: graph drawn and updating, speaker tone marked, Pause and Floor work."""
    if not v.status()["hw"].get("spectrum", True):
        return "skipped: this firmware is built without the spectrum feature (PV_HAS_SPECTRUM=0)"
    reset(v, baro="on", alt=GROUND_M + 300, vz=0)
    p = v.page()
    p.tab("spectrum")
    info = p.wait_text("specinfo", r"^Peak \d+ Hz at -?\d+ dBFS · bin (1\.[89]|2(\.[0-2])?) Hz · [\d.]+ updates/s", 8)
    # (the bin width follows the measured sample rate, so it reads 1.9 to 2.1 Hz)
    painted = ("(()=>{const c=document.getElementById('specCv');const d=c.getContext('2d')"
               ".getImageData(0,0,c.width,c.height).data;let n=0;for(let i=3;i<d.length;i+=4)if(d[i])n++;return n})()")
    if p.js(painted) < 500:
        raise Fail("web page: spectrum graph is blank")
    time.sleep(2)
    rate = float(re.search(r"([\d.]+) updates/s", p.text("specinfo")).group(1))
    if rate < 2:
        raise Fail(f"web page: spectrum updates only {rate}/s")
    v.sim(vz=-3)  # continuous 290 Hz sink alarm
    info = p.wait_text("specinfo", r"speaker tone (28|29)\d Hz \(dashed\)$", 12, "speaker tone marked")
    if a.audio:
        info = p.wait_text("specinfo", r"^Peak (28|29)\d Hz", 6, "graph peak at the tone the speaker plays")
    p.click("#specpause")
    p.wait_text("specpause", r"^Resume$")
    v.sim(vz=0)
    time.sleep(1.5)  # a request already on its way when Pause was pressed may still be drawn
    frozen = p.text("specinfo")
    time.sleep(2.5)
    if p.text("specinfo") != frozen:
        raise Fail("web page: spectrum kept updating while paused")
    p.click("#specpause")
    p.wait_text("specpause", r"^Pause$")
    p.wait("document.getElementById('specinfo').textContent.indexOf('speaker tone')<0", 10,
           "graph resumes and the tone mark clears")
    p.js("(s=>{s.value='60';s.dispatchEvent(new Event('change'))})(document.getElementById('specfloor'))")
    if p.js(painted) < 500:
        raise Fail("web page: spectrum graph blank after changing the floor")
    p.check_fits("spectrum tab")
    return f"{rate:.1f} updates/s, tone mark and peak at 290 Hz, pause and floor ok"


def t_ui_system(v, a):
    """Logs & system tab: tabs switch, debug log viewer, firmware box, system line."""
    reset(v, baro="on", alt=GROUND_M, vz=0)
    st = v.status()
    p = v.page()
    for name in ("flight", "settings", "spectrum", "system", "help"):
        p.tab(name)
        vis = p.js("[...document.querySelectorAll('.page')].filter(x=>!x.hidden).map(x=>x.id)")
        sel = p.js("[...document.querySelectorAll('button[data-tab]')].filter(b=>b.getAttribute('aria-selected')"
                   "==='true').map(b=>b.dataset.tab)")
        if vis != [name + "Page"] or sel != [name]:
            raise Fail(f"web page: tab {name} shows {vis}, selected {sel}")
    # Home Screen app support (iPhone: Share > Add to Home Screen): tags in the page and the icon itself
    tags = p.js("[...document.querySelectorAll('meta')].filter(m=>m.name.startsWith('apple-mobile-web-app')).map(m=>m.name+'='+m.content)")
    if "apple-mobile-web-app-capable=yes" not in tags or "apple-mobile-web-app-title=ParaVario" not in tags:
        raise Fail(f"web page: Home Screen app tags missing: {tags}")
    if p.js("document.querySelector('link[rel=apple-touch-icon]').getAttribute('href')") != "/apple-touch-icon.png":
        raise Fail("web page: apple-touch-icon link missing")
    for path in ("/apple-touch-icon.png", "/favicon.ico"):
        code, body = v._open(path)
        if code != 200 or body[:8] != b"\x89PNG\r\n\x1a\n" or struct.unpack(">II", body[16:24]) != (180, 180):
            raise Fail(f"{path}: expected a 180 x 180 PNG, got HTTP {code}, {len(body)} bytes")
    p.wait_text("fwver", rf"^Installed: {re.escape(st['version'])} · git {re.escape(st['git'])} \(debug build\)$", 5)
    p.wait_text("sys", r"^Uptime \d+:\d\d(:\d\d)? · heap \d+ kB · BLE clients \d+ · baro [\d.]+ hPa$", 5)
    p.wait("document.querySelector('#logs').textContent.length>0 && "
           "document.querySelector('#logs').textContent!=='Loading…'", 5, "flight log list loaded")
    p.js("document.getElementById('dbg').open=true")
    p.wait("document.getElementById('dbglog').childElementCount>5", 8, "debug log lines arriving")
    p.wait_text("dbginfo", r"^debug build · \d+ lines$")
    if not p.js("[...document.querySelectorAll('#dbglog div')].some(d=>/^[EWID]$/.test(d.className))"):
        raise Fail("web page: debug log lines are not coloured by level")
    p.click("#dbgpause")
    p.wait_text("dbgpause", r"^Resume$")
    time.sleep(1.2)
    n1 = p.js("document.getElementById('dbglog').childElementCount")
    time.sleep(2.5)
    if p.js("document.getElementById('dbglog').childElementCount") != n1:
        raise Fail("web page: debug log kept growing while paused")
    p.click("#dbgpause")
    p.wait(f"document.getElementById('dbglog').childElementCount>{n1}", 6, "debug log resumes")
    p.click("#dbgclear")
    if p.js("document.getElementById('dbglog').childElementCount") > 12:
        raise Fail("web page: Clear did not empty the debug log")
    p.click("#fwgo")
    p.wait_text("toast", r"^Choose a \.bin file first$", 3, "Install refused without a file")
    if p.js("document.getElementById('fwpass').type") != "password":
        raise Fail("web page: update password box is not masked")
    p.check_fits("logs & system tab")
    return "5 tabs switch, debug log live/pause/clear, firmware box guarded, version and system line"


def t_ota_guard(v, a):
    """Firmware upload is refused without the right password (nothing is written)."""
    boundary = "paravariotest"
    body = (f"--{boundary}\r\nContent-Disposition: form-data; name=\"file\"; filename=\"firmware.bin\"\r\n"
            "Content-Type: application/octet-stream\r\n\r\n").encode() + b"\xe9not a real image" + \
        f"\r\n--{boundary}--\r\n".encode()
    out = []
    for password in ("definitely-wrong", None):
        m = v.mark()
        req = urllib.request.Request(v.base + "/api/ota", data=body, method="POST")
        req.add_header("Content-Type", "multipart/form-data; boundary=" + boundary)
        if password:
            req.add_header("X-OTA-Password", password)
        try:
            with urllib.request.urlopen(req, timeout=15) as r:
                raise Fail(f"upload with {'a wrong' if password else 'no'} password was accepted (HTTP {r.status})")
        except urllib.error.HTTPError as e:
            reply = e.read().decode("utf-8", "replace")
            if e.code != 401 or "wrong password" not in reply:
                raise Fail(f"expected HTTP 401 'wrong password', got {e.code} {reply[:60]}")
        except Fail:
            raise
        except Exception as e:
            raise Fail(f"upload request failed: {e}")
        v.expect("OTA", r"Update rejected: wrong password", m, timeout=4)
        out.append("wrong" if password else "missing")
    time.sleep(1.5)
    st = v.status()  # still running, not restarting
    if st["uptime"] < 5:
        raise Fail("vario restarted after a refused upload")
    return "refused with a " + " and a ".join(out) + " password; vario kept running"


def t_memory(v, a):
    """Free memory after everything else: not run down, never dangerously low."""
    reset(v)
    time.sleep(6)  # connections from earlier tests are released
    st = v.status()
    start = getattr(v, "heap_start", st["heap"])
    note = (f"free {st['heap'] // 1024} kB (was {start // 1024} kB at the start), lowest since power-on "
            f"{st['heapMin'] // 1024} kB, largest block {st['heapBlock'] // 1024} kB")
    # The test build keeps a larger debug log (96 lines, against 40 in a release build) and has the
    # test runner polling it, so its low-water mark is some 7 kB lower than a release build's would be.
    if st["heapMin"] < 4000:
        raise Fail("free memory fell dangerously low during the tests: " + note)
    if st["heap"] < start - 12000:
        raise Fail("memory was not given back after the tests (leak?): " + note)
    return note


TESTS = [
    ("boot", t_boot), ("settings", t_settings), ("vario_climb", t_vario_climb),
    ("vario_sink", t_vario_sink), ("mute", t_mute), ("auto_volume", t_auto_volume),
    ("gps_states", t_gps_states), ("flight", t_flight), ("baro_only", t_baro_only),
    ("guidance", t_guidance), ("battery", t_battery), ("battery_learner", t_battery_learner),
    ("speech", t_speech), ("volume_ramp", t_volume_ramp), ("glide", t_glide),
    ("variant_no_gps", t_variant_no_gps), ("variant_no_mic", t_variant_no_mic),
    ("ui_flight", t_ui_flight), ("ui_actions", t_ui_actions), ("ui_guidance", t_ui_guidance),
    ("ui_settings", t_ui_settings), ("ui_spectrum", t_ui_spectrum), ("ui_system", t_ui_system), ("ui_help", t_ui_help),
    ("ota_guard", t_ota_guard), ("memory", t_memory),
]


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="run only tests whose name contains one of these")
    ap.add_argument("--host", default="paravario.local")
    ap.add_argument("--no-audio", dest="audio", action="store_false", help="skip microphone checks")
    ap.add_argument("--no-ui", dest="ui", action="store_false", help="skip the web page (browser) tests")
    ap.add_argument("--volume", type=int, default=20, help="speech and vario volume during the tests (%%)")
    ap.add_argument("--memory", action="store_true", help="print the vario's free memory after each test")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every log line received")
    a = ap.parse_args()
    tests = [(n, f) for n, f in TESTS if (not a.names or any(w in n for w in a.names))
             and (a.ui or not n.startswith("ui_"))]
    if a.list:
        for n, f in tests:
            print(f"{n:16} {f.__doc__}")
        return 0

    kill_stray_browsers()
    v = Vario(a.host, a.verbose)
    st = v.status()
    if not st.get("test"):
        print("The vario is not running the test build. Flash it with: pio run -e test -t upload")
        return 2
    print(f"ParaVario {st['version']} ({st['git']}) at {a.host}, {len(tests)} tests"
          f"{'' if a.audio else ', microphone checks off'}")
    v.heap_start = st["heap"]
    saved = v.settings()
    logs_before = {f["n"] for f in v.get_json("/api/logs")["files"]}
    results = []
    try:
        v.set(**TEST_SETTINGS, speechVolume=a.volume, varioVolume=a.volume)
        for name, fn in tests:
            t0 = time.time()
            try:
                note = fn(v, a)
                results.append((name, True, note))
                print(f"  PASS  {name:16} {time.time() - t0:5.1f} s  {note}")
            except Fail as e:
                results.append((name, False, str(e)))
                print(f"  FAIL  {name:16} {time.time() - t0:5.1f} s  {e}")
            if a.memory:
                try:
                    st = v.status()
                    print(f"        memory: free {st['heap']} lowest {st['heapMin']} largest block {st['heapBlock']}")
                except Fail:
                    pass
    finally:
        if a.memory:  # what the vario was doing each time free memory reached a new low
            for l in v.lines:
                mm = re.search(r"-> (\d+) B", l.msg) if l.tag == "MEM" else None
                if mm and int(mm.group(1)) < 24000:
                    print(f"        low memory: {l.msg}")
        v.stop = True
        if v.browser:
            v.browser.close()
        try:
            v.sim(off=1)
            v.sim(reset=1)
            v.set(**saved)
            stray = [f["n"] for f in v.get_json("/api/logs")["files"] if f["n"] not in logs_before]
            for name in stray:  # IGC files a failed test left behind
                v._open("/api/log?name=" + urllib.parse.quote(name), method="DELETE")
            reloads = getattr(v.browser, "reloads", 0) if v.browser else 0
            if reloads:
                print(f"Note: the web page came up without its settings {reloads} time(s) and was reloaded.")
            print("Simulation off, vario settings restored"
                  + (f", {len(stray)} leftover test logs deleted." if stray else "."))
        except Fail as e:
            print(f"WARNING: could not restore the vario: {e}")
    failed = [r for r in results if not r[1]]
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed"
          f"{', ' + str(v.dropped) + ' log lines missed' if v.dropped else ''}")
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
