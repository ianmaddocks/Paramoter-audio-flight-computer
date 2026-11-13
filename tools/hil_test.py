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
import math
import os
import random
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
                req.add_header("X-HIL", "1")  # the vario counts requests without this as somebody else's
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
            self.phone()  # every page test looks at the page as it is on the user's phone (360 CSS pixels wide)
            self.cmd("Network.enable")
            self.cmd("Network.setExtraHTTPHeaders", headers={"X-HIL": "1"})  # the harness's browser is not 'somebody else'
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

    def inspect(self, where):
        """What a look at the screen would catch: text cut off ("39 k..."), things drawn over each other, anything off the
        edge of the screen. Looks at the page that is showing."""
        bad = self.js(INSPECT_JS)
        if bad:
            raise Fail(f"web page: {where}: " + "; ".join(bad[:6]))

    def screenshot(self, name):
        """Saves what the page looks like now (PNG, results/ui/<name>.png) so that it can be looked at after the run."""
        import base64
        os.makedirs(SHOT_DIR, exist_ok=True)
        r = self.cmd("Page.captureScreenshot", format="png")
        path = os.path.join(SHOT_DIR, name + ".png")
        with open(path, "wb") as f:
            f.write(base64.b64decode(r["data"]))
        return path

    def phone(self, width=360, height=800):
        """Makes the page believe it is on a phone of this width (CSS pixels, 2x pixel density): the user's phone is 360."""
        self.cmd("Emulation.setDeviceMetricsOverride", width=width, height=height, deviceScaleFactor=2, mobile=True)

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


SHOT_DIR = os.path.join("results", "ui")
INSPECT_JS = r"""(()=>{const bad=[];
const vis=e=>{const r=e.getBoundingClientRect(),cs=getComputedStyle(e);return r.width>0&&r.height>0&&cs.visibility!=='hidden'&&cs.display!=='none'};
const name=e=>e.id||(typeof e.className==='string'&&e.className)||e.tagName;
const shown='.page:not([hidden]) ';
document.querySelectorAll(shown+'.v:not(.vrow),'+shown+'.s:not(.srow),'+shown+'.vrow span,'+shown+'.srow span,'+shown+'.k,'+shown+'button,'+shown+'.alert,#ready,'+shown+'summary').forEach(e=>{
  if(!vis(e)||e.closest('svg'))return;const cs=getComputedStyle(e);
  if(e.scrollWidth>e.clientWidth+1&&(cs.overflow!=='visible'||cs.textOverflow==='ellipsis'))bad.push('text cut off in '+name(e)+': "'+e.textContent.trim().slice(0,40)+'"')});
document.querySelectorAll(shown+'.vrow,'+shown+'.srow,'+shown+'.btns').forEach(row=>{
  const k=[...row.children].filter(vis);
  for(let i=0;i<k.length;i++)for(let j=i+1;j<k.length;j++){const a=k[i].getBoundingClientRect(),b=k[j].getBoundingClientRect();
    if(a.left<b.right-1&&b.left<a.right-1&&a.top<b.bottom-1&&b.top<a.bottom-1)bad.push('overlap in '+name(row)+': '+name(k[i])+' and '+name(k[j]))}});
document.querySelectorAll(shown+'*').forEach(e=>{if(!vis(e)||e.closest('svg')&&e.tagName!=='svg')return;const r=e.getBoundingClientRect();
  if(r.right>innerWidth+1||r.left<-1)bad.push('off screen: '+name(e))});
return [...new Set(bad)]})()"""

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
    igcEnabled=True, igcPreS=20, igcQnhAlt=True, gpsMode=1,
    rpmPulsesPerRev=1.0, rpmMin=1200, rpmMax=10000, rpmMinConfidence=3.0, spectrumEnabled=True,
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


def tone_mode(v):
    """What the vario last told its tone generator, from the log: 'startup', 'climb', 'sink' or 'off'
    (None if no tone line has been seen at all)."""
    v.poll()
    for l in reversed(v.lines):
        if l.tag == "TONE":
            return l.msg.split()[0]
    return None


def silent_for(v, seconds, what):
    """The vario stays silent for `seconds`: no climb or sink tone is commanded. The log feed must have
    been alive meanwhile (NAV lines arriving), so that silence is not merely an empty log."""
    m = v.mark()
    v.never("TONE", r"^(climb|sink)", m, seconds)
    nav = sum(1 for l in v.lines[m:] if l.tag == "NAV")
    if nav < max(2, int(seconds)):
        raise Fail(f"{what}: only {nav} NAV lines arrived in {seconds:.0f} s, so the silence proves nothing")
    if tone_mode(v) not in ("off", None):
        raise Fail(f"{what}: the tone generator was last told '{tone_mode(v)}'")


def last_tone_hz(v, since, kind):
    """Pitch of the most recent `kind` TONE line after the mark, or None."""
    for l in reversed(v.lines[since:]):
        if l.tag == "TONE" and l.msg.startswith(kind + " "):
            return float(l.msg.split()[1])
    return None


@no_baro_takeoff
def t_vario_thresholds(v, a):
    """Climb and sink tones switch on either side of their thresholds, at the pitch the firmware
    calculates, and off again. Speeds are comfortably either side of each threshold (not at it: the
    barometer filter makes exact boundaries unreliable here; the exact comparisons are checked by
    vario_logic). The silent cases need the log feed alive and, with the microphone, an actually quiet
    speaker, so an empty log cannot pass."""
    cfg = v.settings()
    climb_th, sink_th = cfg["climbThreshold"], cfg["sinkThreshold"]
    base, rise = cfg["varioBaseHz"], cfg["varioHzPerMs"]
    if not (0.15 <= climb_th <= 2.0 and -8.0 <= sink_th <= -1.0):
        raise Fail(f"thresholds {climb_th} / {sink_th} m/s leave no room for the test's margins (0.05 / 0.2 m/s)")

    # The firmware's tone formulas (variosound::toneFor, checked exactly by vario_logic)
    def climb_hz(vz):
        return min(2500.0, max(200.0, base + rise * vz))

    def sink_hz(vz):
        return min(1000.0, max(150.0, base * 0.5 + 20.0 * vz))

    def hear(hz, what, want):
        """With the microphone: the tone is really (not) coming out of the speaker."""
        if not a.audio:
            return ""
        bin_hz, db = v.listen(2.0)
        got, fl = level_at(bin_hz, db, hz), floor_of(db)
        if want and got - fl < 12:
            raise Fail(f"microphone did not hear the {what} at {hz:.0f} Hz: {got:.0f} dBFS, floor {fl:.0f}")
        if not want and got - fl > 10:
            raise Fail(f"microphone heard {got - fl:.0f} dB over the floor at {hz:.0f} Hz where {what} should be silent")
        return f", heard {got - fl:.0f} dB over floor" if want else ""

    def reads(target, tol, what):
        return v.wait_nav(lambda n: abs(float(n["vs"]) - target) < tol, 16, what)

    notes = []

    # ---- stationary: no tone at all
    reset(v, baro="on", alt=GROUND_M, vz=0)
    time.sleep(7)  # the reset restarts the barometer filter; the vario reads 0 until it has settled
    reads(0.0, 0.1, "vario reading 0 m/s")
    silent_for(v, 3.0, "stationary")
    hear(climb_hz(0.0), "the stationary vario", False)

    # ---- climb threshold
    below = climb_th - 0.05
    v.sim(vz=below)
    reads(below, 0.04, f"vario reading {below:.2f} m/s")
    silent_for(v, 3.0, f"climbing at {below:.2f} m/s, under the {climb_th:.2f} m/s threshold")
    hear(climb_hz(below), f"{below:.2f} m/s of climb", False)

    above = climb_th + 0.05
    m = v.mark()
    v.sim(vz=above)
    v.expect("TONE", r"^climb (\d+) Hz", m, timeout=16, what=f"climb tone at {above:.2f} m/s, over the threshold")
    reads(above, 0.04, f"vario reading {above:.2f} m/s")
    time.sleep(1.5)  # the pitch is logged when it has moved 10 Hz: let it follow the reading
    hz = last_tone_hz(v, m, "climb")
    near(hz, climb_hz(above), 20, f"climb tone pitch at {above:.2f} m/s")
    if tone_mode(v) != "climb":
        raise Fail(f"after the climb tone started the vario was told '{tone_mode(v)}'")
    heard = hear(hz, "the climb tone", True)
    m = v.mark()
    v.sim(vz=0)
    v.expect("TONE", r"^off", m, timeout=10, what="climb tone switches off when the climb stops")
    reads(0.0, 0.1, "vario back at 0 m/s")
    if tone_mode(v) != "off":
        raise Fail(f"after the climb stopped the vario was told '{tone_mode(v)}', expected off")
    notes.append(f"climb: silent at {below:.2f}, {hz:.0f} Hz at {above:.2f} (expected {climb_hz(above):.0f}){heard}")

    # ---- sink threshold
    reset(v, baro="on", alt=GROUND_M + 300, vz=0)
    time.sleep(7)
    reads(0.0, 0.1, "vario reading 0 m/s")
    sink_above = sink_th + 0.2  # still too gentle for the alarm
    v.sim(vz=sink_above)
    reads(sink_above, 0.05, f"vario reading {sink_above:.2f} m/s")
    silent_for(v, 3.0, f"sinking at {-sink_above:.2f} m/s, under the {-sink_th:.2f} m/s threshold")
    hear(sink_hz(sink_above), f"{-sink_above:.2f} m/s of sink", False)

    sink_below = sink_th - 0.2
    m = v.mark()
    v.sim(vz=sink_below)
    v.expect("TONE", r"^sink (\d+) Hz", m, timeout=16, what=f"sink tone at {-sink_below:.2f} m/s, past the threshold")
    reads(sink_below, 0.06, f"vario reading {sink_below:.2f} m/s")
    time.sleep(1.5)
    hz = last_tone_hz(v, m, "sink")
    near(hz, sink_hz(sink_below), 12, f"sink tone pitch at {-sink_below:.2f} m/s")
    if tone_mode(v) != "sink":
        raise Fail(f"after the sink tone started the vario was told '{tone_mode(v)}'")
    heard = hear(hz, "the sink tone", True)
    m = v.mark()
    v.sim(vz=0)
    v.expect("TONE", r"^off", m, timeout=10, what="sink tone switches off when the sink stops")
    reads(0.0, 0.1, "vario back at 0 m/s")
    if tone_mode(v) != "off":
        raise Fail(f"after the sink stopped the vario was told '{tone_mode(v)}', expected off")
    notes.append(f"sink: silent at {sink_above:.2f}, {hz:.0f} Hz at {sink_below:.2f} (expected {sink_hz(sink_below):.0f}){heard}")
    return "; ".join(notes)


def t_vario_logic(v, a):
    """Software-level unit test of the tone decision (variosound::toneFor), run on the vario with
    values of its own: the exact comparisons at the climb and sink thresholds (>= and <=, one float
    step either side), the pitch and beep-rate formulas with their limits, and every condition that
    silences the tone. No sensors, filtering, timing or sound are involved."""
    m = v.mark()
    v.sim(tonetest=1)
    _, mm = v.expect("TONET", r"^done pass=(\d+) fail=(\d+)", m, timeout=8, what="the unit test's result line")
    passed, failed = int(mm.group(1)), int(mm.group(2))
    fails = [l.msg for l in v.lines[m:] if l.tag == "TONET" and l.msg.startswith("FAIL")]
    if failed or fails:
        raise Fail(f"{failed} of {passed + failed} tone-logic cases failed: " + " | ".join(fails[:3]))
    if passed < 40:
        raise Fail(f"only {passed} tone-logic cases ran (expected at least 40): the table did not run in full")
    return f"{passed} cases passed: thresholds exact (>= climb, <= sink), formulas and limits, all silencing conditions"


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


def unit_suite(v, name, min_cases):
    """Runs an on-device unit suite (src/test/*Test.cpp) and fails on any wrong case."""
    r = v.get_json("/api/sim?unit=" + name)
    if r["fail"] or r["pass"] < min_cases:
        raise Fail(f"unit suite {name}: {r['pass']} passed, {r['fail']} failed (expected at least {min_cases} cases): "
                   + " | ".join(r["failures"][:3]))
    return r["pass"]


def rpm_hz(rpm, ppr=1.0):
    """Engine firing frequency for the synthetic microphone."""
    return rpm * ppr / 60.0


def rpm_now(v):
    m = v.status()["mic"]
    return float(m["rpm"]), float(m["conf"])


def rpm_hold(v, lo, hi, seconds, what, every=0.4):
    """The RPM reading stays within [lo, hi] for `seconds`; returns the readings."""
    out = []
    end = time.time() + seconds
    while time.time() < end:
        r, _ = rpm_now(v)
        out.append(r)
        if not lo <= r <= hi:
            raise Fail(f"{what}: RPM read {r:.0f}, outside {lo:.0f}..{hi:.0f} (readings so far {[round(x) for x in out]})")
        time.sleep(every)
    return out


def rpm_settle(v, target, tol, timeout, what):
    """Waits for the RPM reading to come within tol (fraction) of target; returns the seconds it took."""
    t0 = time.time()
    last = None
    while time.time() - t0 < timeout:
        last, _ = rpm_now(v)
        if abs(last - target) <= tol * target:
            return time.time() - t0
        time.sleep(0.3)
    raise Fail(f"{what}: RPM still {last:.0f} after {timeout:.0f} s, wanted {target:.0f} +-{tol * 100:.0f}%")


def t_rpm_logic(v, a):
    """Software-level unit test of the RPM estimate (mic::rpmFromSpectrum) on synthetic spectra, run on the
    vario: known speeds across the range, pulses per revolution, weak fundamental, noise only (many
    patterns), silence, lone tone against a harmonic series, range limits."""
    n = unit_suite(v, "rpm", 20)
    return f"{n} cases passed: speeds 1500..8000 rpm within 1.5 %, pulses per revolution, noise and silence give no RPM"


def t_geo_logic(v, a):
    """Software-level unit test of the geometry helpers (nav/Geo.h): angle wrapping, distance, bearings, local
    coordinates, vector bearings, clock positions."""
    n = unit_suite(v, "geo", 30)
    return f"{n} cases passed: wrap, distance (incl. date line), bearings, local offsets, vector bearing, clock positions"


def t_wind_logic(v, a):
    """Software-level unit test of the wind estimator (flight/Wind.cpp): circling fits (accuracy, both turn
    directions, 0/360, zero wind, noise), no fit from too little turn, refusals, averaging, glide over the
    ground for head/tail/cross wind."""
    n = unit_suite(v, "wind", 45)
    return f"{n} cases passed: fit accuracy, turn direction, 0/360, noise, no-fit and refused cases, averaging, ground glide"


def t_circuit_logic(v, a):
    """Software-level unit test of the landing circuit (flight/Approach.cpp) driven by scripted positions: leg
    order, turn points, base turn from height (clamps), overshoot, wrong way, refusals; left and right, two
    wind directions."""
    n = unit_suite(v, "circuit", 40)
    return f"{n} cases passed: leg order, turn points, base turn from height, overshoot, wrong way, refusals"


def t_battery_logic(v, a):
    """Software-level unit test of the battery arithmetic (sensors/Battery.cpp) and the low-battery call
    (app/Alerts.cpp): mV to volts, smoothing, percentage curve, 10 s hold, 5 min repeat, USB and 0 V."""
    n = unit_suite(v, "battery", 30)
    return f"{n} cases passed: conversion, smoothing, curve, low-battery timing and exceptions"


def t_rpmtrack_logic(v, a):
    """Software-level unit test of the RPM reading logic (sensors/RpmTrack.h): a jump needs a second agreeing
    window, a lone bad window never shows (F4), the reading is held through speech and drops after the hold time
    without usable windows (F3)."""
    n = unit_suite(v, "rpmtrack", 30)
    return f"{n} cases passed: smoothing, jump confirmation, dropout hold, speech hold"


def t_rpm_speech(v, a):
    """The vario speaks for several seconds with the engine running (F3): the RPM reading must not drop to 0 or move
    while it does, and must still follow a real change right after. Uses the 'test voice' announcement."""
    cfg = v.settings()
    ppr = cfg["rpmPulsesPerRev"]
    notes = []
    try:
        v.sim(micMode="synth", micHz=rpm_hz(2500, ppr), micAmp=150, micNoise=60)
        rpm_settle(v, 2500, 0.03, 8, "engine at 2500 rpm")
        for round_ in range(3):
            m = v.mark()
            v.action("test_voice")
            v.expect("SAY", r"system_ready", m, timeout=4, what="announcement started")
            readings = []
            end = time.time() + 9.0  # the announcement takes about 6 s
            while time.time() < end:
                r_, _ = rpm_now(v)
                readings.append(r_)
                time.sleep(0.15)
            bad = [x for x in readings if abs(x - 2500) > 0.06 * 2500]
            if bad:
                raise Fail(f"announcement {round_ + 1}: RPM read {sorted(set(round(x) for x in bad))} during or after the speech "
                           f"(engine at 2500 rpm): {[round(x) for x in readings]}")
        notes.append(f"3 announcements of about 6 s: RPM held at 2500 (+-6 %) throughout, {len(readings)} readings each")
        # a real change straight after speech is followed
        v.action("test_voice")
        time.sleep(1.0)
        v.sim(micHz=rpm_hz(3500, ppr))
        t = rpm_settle(v, 3500, 0.04, 14, "engine raised to 3500 rpm during/after speech")
        notes.append(f"change to 3500 during speech followed within {t:.1f} s")
    finally:
        v.sim(micMode="real", micHz=0, micNoise=0)
    return "; ".join(notes)


def t_rpm_glitch(v, a):
    """One analysis window reports a wrong RPM with high confidence (F4): up, down and an octave. The reading must
    not show it; two windows in a row that agree (the engine really changed) must be followed."""
    cfg = v.settings()
    ppr = cfg["rpmPulsesPerRev"]
    notes = []
    try:
        v.sim(micMode="synth", micHz=rpm_hz(2000, ppr), micAmp=150, micNoise=60)
        rpm_settle(v, 2000, 0.03, 8, "engine at 2000 rpm")
        seen = []
        for bad in (4000, 1000, 6000, 3000, 4000):
            v.sim(rpmGlitch=bad)
            end = time.time() + 1.2
            while time.time() < end:
                r_, _ = rpm_now(v)
                seen.append(r_)
                time.sleep(0.05)
        worst = max(abs(x - 2000) for x in seen)
        if worst > 120:
            raise Fail(f"a single bad window reached the reading: readings up to {worst:.0f} rpm away from 2000: "
                       f"{sorted(set(round(x) for x in seen))}")
        notes.append(f"5 single bad windows (4000, 1000, 6000, 3000, 4000 rpm): reading stayed within {worst:.0f} rpm of 2000 "
                     f"over {len(seen)} readings")
        v.sim(micHz=rpm_hz(3600, ppr))
        t = rpm_settle(v, 3600, 0.04, 8, "a real change to 3600 rpm")
        notes.append(f"a real change to 3600 followed in {t:.1f} s")
    finally:
        v.sim(micMode="real", micHz=0, micNoise=0)
    return "; ".join(notes)


def t_flight_logic(v, a):
    """Software-level unit test of the landing decision (flight/Flight.cpp, looksStill and landingHoldMs): trusted ground
    speed, weak or lost GPS, the height and vertical-speed limits, no evidence at all (F1, F8), and how much longer
    a still aircraft has to wait at height than near the ground (F10)."""
    n = unit_suite(v, "flight", 40)
    return f"{n} cases passed: speed rule unchanged, no landing from a missing or weak GPS at height, limits exact"


def t_baro_logic(v, a):
    """Software-level unit test of the barometer plausibility checks (sensors/BaroGuard.h): pressure limits and
    NaN, lone outliers held back, persistent steps accepted once, the rate limit, gaps, frozen readings and
    recovery (F7)."""
    n = unit_suite(v, "baro", 29)
    return f"{n} cases passed: impossible values, lone outliers, steps, rate limit, frozen reading, recovery"


def t_rpm(v, a):
    """Engine RPM from the microphone, with a synthetic engine fed in place of the ADC (known firing
    frequency, so the right answer is known): stopped, idle, up, down, rapid changes, noise only, noise
    over the engine, a dead input, and recovery. The reading is read from the status; the engine's effect
    on other features is nil by design (RPM is shown, not used), so there is nothing else to assert."""
    cfg = v.settings()
    if cfg["rpmPulsesPerRev"] != 1.0:
        raise Fail("test expects 1 pulse per revolution")
    ppr = cfg["rpmPulsesPerRev"]
    notes = []
    try:
        reset(v, baro="on", alt=GROUND_M, vz=0)
        # ---- stopped: a dead input, then silence with noise only
        v.sim(micMode="dead")
        time.sleep(3)
        rpm_hold(v, 0, 0, 3, "dead microphone input", 0.5)
        v.sim(micMode="synth", micHz=0, micAmp=150, micNoise=60)
        time.sleep(2)
        rpm_hold(v, 0, 0, 6, "engine stopped (noise only)", 0.5)
        notes.append("stopped: 0")

        # ---- idle, then steady
        v.sim(micMode="synth", micHz=rpm_hz(2000, ppr), micAmp=150, micNoise=60)
        t = rpm_settle(v, 2000, 0.03, 8, "engine started at 2000 rpm")
        r = rpm_hold(v, 1940, 2060, 6, "steady 2000 rpm")
        notes.append(f"idle 2000 found in {t:.1f} s, steady {min(r):.0f}..{max(r):.0f}")

        # ---- increasing
        for target in (2600, 3200, 4000, 5000):
            v.sim(micHz=rpm_hz(target, ppr))
            t = rpm_settle(v, target, 0.03, 8, f"engine raised to {target} rpm")
        rpm_hold(v, 4850, 5150, 3, "steady 5000 rpm")
        notes.append("up 2000->5000 followed in steps")

        # ---- decreasing
        for target in (3800, 2800, 2000):
            v.sim(micHz=rpm_hz(target, ppr))
            rpm_settle(v, target, 0.03, 8, f"engine lowered to {target} rpm")
        notes.append("down 5000->2000 followed in steps")

        # ---- rapid changes: no reading outside the two speeds (no octave errors, no spikes); zero is
        #      acceptable while the window holds both
        seen = []
        for i in range(16):
            v.sim(micHz=rpm_hz(2000 if i % 2 == 0 else 4000, ppr))
            end = time.time() + 0.5
            while time.time() < end:
                r, _ = rpm_now(v)
                seen.append(r)
                if r != 0 and not (1800 <= r <= 4400):
                    raise Fail(f"rapid changes: RPM read {r:.0f}, between nothing and the two speeds")
                time.sleep(0.2)
        v.sim(micHz=rpm_hz(2000, ppr))
        rpm_settle(v, 2000, 0.03, 8, "settled at 2000 rpm after rapid changes")
        notes.append(f"rapid 2000/4000 alternation: {len(seen)} readings, none outside 1800..4400 "
                     f"({sum(1 for x in seen if x == 0)} zero)")

        # ---- noise over the engine, and noise alone
        v.sim(micNoise=100)
        time.sleep(2)
        rpm_hold(v, 1940, 2060, 5, "2000 rpm under noise (noise amplitude 100 against engine 150)")
        v.sim(micHz=0, micNoise=300)
        time.sleep(1.8)  # hold time 1.5 s
        rpm_hold(v, 0, 0, 6, "loud noise, no engine: a false RPM")
        notes.append("noise: engine kept at 2000 under noise; loud noise alone gives 0")

        # ---- missing input while running, then recovery
        v.sim(micMode="synth", micHz=rpm_hz(3000, ppr), micNoise=60)
        rpm_settle(v, 3000, 0.03, 8, "engine at 3000 rpm")
        v.sim(micMode="dead")
        t0 = time.time()
        while rpm_now(v)[0] != 0:
            if time.time() - t0 > 6:
                raise Fail(f"input lost: RPM still {rpm_now(v)[0]:.0f} after 6 s")
            time.sleep(0.3)
        notes.append(f"input lost -> 0 after {time.time() - t0:.1f} s")
        v.sim(micMode="synth", micHz=rpm_hz(3000, ppr), micNoise=60)
        t = rpm_settle(v, 3000, 0.03, 8, "input recovered")
        notes.append(f"recovered in {t:.1f} s")
    finally:
        v.sim(micMode="real", micHz=0, micNoise=0)
    return "; ".join(notes)


# ---- wind ------------------------------------------------------------------------------------
# The simulator can fly an aircraft (sim keys fly, hdg, air, windFrom, windSpd, turn, turnTo, turnRate):
# ground speed and track, as the GPS reports them, follow from heading, airspeed and the wind blowing
# FROM windFrom, so the true wind is always known.

def wind_vec(frm, spd):
    """Wind as the vector it blows along (east, north), from the bearing it comes FROM."""
    t = math.radians(frm + 180.0)
    return spd * math.sin(t), spd * math.cos(t)


def wind_now(v):
    return v.status()["wind"]


def wind_err(w, truth_vec):
    """Distance (m/s) between the vario's wind estimate and a wind vector."""
    e, n = wind_vec(w["from"], w["speed"])
    return math.hypot(e - truth_vec[0], n - truth_vec[1])


def circ_diff(a, b):
    return abs((a - b + 180.0) % 360.0 - 180.0)


def airborne(v, hdg, air, wfrom, wspd, climb=60.0, vz=2.0):
    """Takeoff in the simulated aircraft (flying into the wind when hdg == wfrom), level at climb m up."""
    on_ground_with_fix(v)
    v.sim(fly=1, hdg=hdg, air=air, windFrom=wfrom, windSpd=wspd, turn=0)
    m = v.mark()
    time.sleep(5)  # the sudden speed is "suspect" for 3 s before it counts
    v.sim(vz=vz, to=GROUND_M + climb)
    v.expect("FLT", r"^takeoff \(gps\)", m, timeout=15, what="takeoff detected")
    v.expect("SIM", r"reached target altitude", m, timeout=climb / vz + 30, what="level at the cruise height")
    return m


def next_fit(v, what, timeout=70):
    m = v.mark()
    v.expect("WIND", r"^Wind fit:", m, timeout=timeout, what=what)
    time.sleep(0.5)
    return wind_now(v)


def t_wind(v, a):
    """Wind estimate: the takeoff run first, then circling. A simulated aircraft flies at 10 m/s in a known
    wind: head, tail and cross winds read back as the right ground speed and track (conventions), the
    takeoff estimate is the direction taken off into, and circling fits give the true wind (from, not
    towards; either turn direction; 0/360 wrap; zero wind; changes of wind). The firmware averages each
    new fit with the previous estimate, so the expected value after every fit is the mean of the two."""
    notes = []
    try:
        airborne(v, hdg=270, air=10, wfrom=270, wspd=4)
        w = wind_now(v)
        if not w["valid"] or w["src"] != "takeoff" or circ_diff(w["from"], 270) > 3:
            raise Fail(f"after taking off into a wind from 270 the estimate is {w}")
        near(float(v.nav()["gs"]), 6.0, 0.3, "ground speed into a 4 m/s wind at 10 m/s airspeed")
        notes.append("takeoff run: from 270, source takeoff")

        # ---- conventions: what each wind direction does to ground speed and track (aircraft heading north)
        v.sim(hdg=0, windSpd=5)
        for frm, name, gs_want in ((0, "head", 5.0), (180, "tail", 15.0), (270, "cross from the left", math.hypot(10, 5)),
                                   (90, "cross from the right", math.hypot(10, 5))):
            v.sim(windFrom=frm)
            n = v.wait_nav(lambda n: abs(float(n["gs"]) - gs_want) < 0.4, 8, f"{name} wind reflected in ground speed")
            trk = float(n["trk"])
            if frm == 270 and not 22 <= trk <= 31:
                raise Fail(f"wind from the left of a northbound aircraft: track {trk:.0f}, expected about 27 (drifted right)")
            if frm == 90 and not 329 <= trk <= 338:
                raise Fail(f"wind from the right of a northbound aircraft: track {trk:.0f}, expected about 333 (drifted left)")
            if frm in (0, 180) and circ_diff(trk, 0) > 2:
                raise Fail(f"{name} wind moved the track to {trk:.0f}")
        if wind_now(v)["src"] != "takeoff":
            raise Fail(f"straight flight changed the wind estimate: {wind_now(v)}")
        notes.append("head, tail and cross winds give the right ground speed and drift; straight flight leaves the estimate")

        # ---- circling fits. The wind changes right after each fit so the next one is all new wind.
        #      (The straight legs above left samples in the 60 s window; they would bias the first fit.)
        prev = wind_vec(wind_now(v)["from"], wind_now(v)["speed"])
        scenarios = [
            (270, 5.0, 18, "wind from 270 at 5 m/s, right turns"),
            (355, 7.0, -18, "wind from 355 at 7 m/s, left turns (0/360 wrap)"),
            (5, 3.0, 18, "wind from 5 at 3 m/s, right turns"),
            (180, 8.0, -18, "wind from 180 at 8 m/s, left turns"),
            (90, 0.0, 18, "no wind"),
            (225, 6.0, 18, "wind from 225 at 6 m/s"),
        ]
        for i, (frm, spd, turn, what) in enumerate(scenarios):
            # the new wind first on a straight leg (a point on its circle), then the samples so far are dropped
            # and the circle starts: no sample of the old wind can be in the fit
            v.sim(turn=0, windFrom=frm, windSpd=spd)
            time.sleep(2.5)
            v.sim(windflush=1, turn=turn)
            w = next_fit(v, f"a wind fit: {what}")
            if w["src"] != "circling":
                raise Fail(f"{what}: source is {w['src']} after a fit")
            truth = wind_vec(frm, spd)
            if i == 0:  # the first fit replaces the takeoff estimate outright
                want, tol = truth, 0.3
            else:
                want = ((prev[0] + truth[0]) / 2, (prev[1] + truth[1]) / 2)
                tol = 0.4
            got = wind_vec(w["from"], w["speed"])
            err = math.hypot(got[0] - want[0], got[1] - want[1])
            if err > tol:
                raise Fail(f"{what}: estimate from {w['from']:.0f} at {w['speed']:.1f} m/s is {err:.2f} m/s away from the "
                           f"expected from {math.degrees(math.atan2(-want[0], -want[1])) % 360:.0f} at {math.hypot(*want):.1f} m/s")
            prev = got
            notes.append(f"{what} -> from {w['from']:.0f} at {w['speed']:.1f}")

        # ---- convergence: with the wind held, each further fit halves the remaining error
        truth = wind_vec(225, 6.0)
        errs = [wind_err(wind_now(v), truth)]
        for _ in range(3):
            next_fit(v, "further fit in steady wind")
            errs.append(wind_err(wind_now(v), truth))
        if not (errs[-1] < errs[0] * 0.5 + 0.2 and errs[-1] < 1.0):
            raise Fail(f"steady wind: error did not converge, {[round(e, 2) for e in errs]} m/s over successive fits")
        notes.append("steady wind: error " + " -> ".join(f"{e:.2f}" for e in errs) + " m/s over successive fits")
    finally:
        v.sim(turn=0)
    return "; ".join(notes)


def no_fit(v, seconds, what, flush=True):
    """No wind fit is logged for `seconds`, and the estimate does not move."""
    if flush:
        v.sim(windflush=1)  # samples from the previous manoeuvre would count towards this one
    before = wind_now(v)
    m = v.mark()
    v.never("WIND", r"^Wind fit:", m, seconds)
    after = wind_now(v)
    if (before["from"], before["speed"], before["src"]) != (after["from"], after["speed"], after["src"]):
        raise Fail(f"{what}: the estimate moved from {before} to {after} without a fit")
    return after


def t_wind_faults(v, a):
    """Wind estimate when the data is not good enough, or the GPS is not there: flying straight, half a
    circle, a turn too slow to cover 270 degrees inside the 60 s window, a wind too strong or an airspeed
    too low to be believed, GPS loss during a circle, manual wind taking over. Nothing may move the estimate
    or invent a fit; after a loss the next complete circle gives the right wind again."""
    notes = []
    truth = wind_vec(270, 5.0)
    try:
        airborne(v, hdg=270, air=10, wfrom=270, wspd=5)
        v.sim(turn=18)
        w = next_fit(v, "first fit to establish a wind")
        if wind_err(w, truth) > 0.4:
            raise Fail(f"baseline fit is {w}")
        base = w
        notes.append(f"baseline from {w['from']:.0f} at {w['speed']:.1f}")

        # ---- not enough turn
        v.sim(turn=0)
        no_fit(v, 25, "flying straight")
        notes.append("straight flight: no fit")
        v.sim(windflush=1, turnTo=90)  # a half circle, then straight on
        time.sleep(12)
        no_fit(v, 30, "half a circle", flush=False)
        notes.append("half circle: no fit")
        # 2.5 deg/s is 150 degrees of heading in the 60 s window. (3 deg/s, 180 degrees, is not safe: in a headwind
        # the ground track turns up to twice as fast as the heading, so 180 degrees of heading can cover 9 of the 12
        # direction bins and give a legitimate fit: the baseline run of ae9b1a0 failed on exactly that.)
        v.sim(windflush=1, turn=2.5)
        no_fit(v, 70, "a very slow turn", flush=False)
        notes.append("2.5 deg/s turn (150 degrees in the 60 s window): no fit")
        v.sim(turn=0)

        # ---- data that is not believed: logged as rejected, estimate untouched
        for what, kw, why in (("a 16 m/s wind at 20 m/s airspeed (a wind above the 15 m/s limit; a wind over the airspeed gives no circle at all)", dict(windSpd=16.0, air=20.0), "wind"),
                              ("4 m/s airspeed", dict(windSpd=0.0, air=4.0), "airspeed")):
            v.sim(windflush=1, turn=18, **kw)
            m = v.mark()
            v.expect("WIND", r"fit rejected", m, timeout=75, what=f"rejection of {what}")
            if v.find("WIND", r"^Wind fit:", m):
                raise Fail(f"{what}: a fit was accepted")
            now = wind_now(v)
            if (now["from"], now["speed"]) != (base["from"], base["speed"]):
                raise Fail(f"{what}: estimate moved from {base} to {now} though the fit was rejected")
            notes.append(f"{what}: rejected, estimate kept")
        v.sim(windflush=1, windSpd=5.0, air=10.0, turn=18)

        # ---- GPS lost during circling: the gap leaves holes in the circle, no fit may come from it before
        #      enough directions are covered, and the estimate is right again after
        w = next_fit(v, "fit before the GPS loss")  # a fit just happened, so a fresh collection starts now
        m0 = v.mark()
        for mode in ("silent", "nofix"):
            v.sim(gps=mode)
            time.sleep(7)  # over a third of a 20 s circle without a position. (Much longer and level flight with no
            #                GPS is taken for a landing after 2 x the landing time: see docs/TEST_FINDINGS.md)
            v.sim(gps="fix")
            v.never("WIND", r"^Wind fit:", m0, 4)  # the gap leaves too few directions for a fit
            m0 = v.mark()
        w = next_fit(v, "fit after the GPS came back", timeout=80)
        if wind_err(w, truth) > 0.6:
            raise Fail(f"after the GPS loss the estimate is {w}, true wind from 270 at 5 m/s")
        notes.append("GPS silent / no fix for 7 s in a circle: no fit from the gap, correct again after")

        # ---- manual wind overrides everything and the circling estimate does not show through
        v.set(windManual=True, windFromDeg=135, windSpeedMs=3.0)
        try:
            w = wind_now(v)
            if w["src"] != "manual" or circ_diff(w["from"], 135) > 0.5 or abs(w["speed"] - 3.0) > 0.05:
                raise Fail(f"manual wind 135 / 3 m/s reads back as {w}")
            time.sleep(25)  # a fit would run in this time
            w = wind_now(v)
            if w["src"] != "manual" or circ_diff(w["from"], 135) > 0.5:
                raise Fail(f"circling changed a manual wind: {w}")
            notes.append("manual wind used and kept while circling")
        finally:
            v.set(windManual=TEST_SETTINGS["windManual"], windFromDeg=0, windSpeedMs=0)
    finally:
        v.sim(turn=0)
    return "; ".join(notes)


def t_wind_change(v, a):
    """A change of wind in flight (F2): after a fit in one wind the aircraft flies straight for 8 s (samples of the
    old wind stay in the window), the wind changes to the opposite direction and the circling goes on. The first
    fit in the new wind must not be biased by the old samples (the firmware averages it with the old estimate,
    so the expected value is the mean of the two), and the second, which agrees with the first, must give the
    new wind itself rather than a further half step. A fit in the third wind then goes back the same way."""
    notes = []
    try:
        airborne(v, hdg=270, air=10, wfrom=270, wspd=4)
        v.sim(windflush=1, windSpd=5.0, turn=18)
        w = next_fit(v, "fit in the first wind")
        a_vec = wind_vec(270, 5.0)
        if wind_err(w, a_vec) > 0.6:
            raise Fail(f"first wind: estimate {w}, true wind from 270 at 5 m/s")
        est = wind_vec(w["from"], w["speed"])
        notes.append(f"wind A from 270 at 5 -> from {w['from']:.0f} at {w['speed']:.1f}")

        v.sim(turn=0)
        time.sleep(8)  # straight: about 16 samples of the old wind in the window
        v.sim(windFrom=90, windSpd=5.0, turn=18)
        b_vec = wind_vec(90, 5.0)
        w1 = next_fit(v, "first fit after the change", timeout=90)
        want = ((est[0] + b_vec[0]) / 2, (est[1] + b_vec[1]) / 2)
        err = wind_err(w1, want)
        if err > 0.6:
            raise Fail(f"first fit after the wind change is {err:.2f} m/s from the mean of the old estimate and the new "
                       f"wind (from {w1['from']:.0f} at {w1['speed']:.1f}); old samples biasing it? (F2)")
        notes.append(f"first fit after the change: {err:.2f} m/s from the expected mean")

        w2 = next_fit(v, "second fit in the new wind", timeout=90)
        err2 = wind_err(w2, b_vec)
        if err2 > 0.6:
            raise Fail(f"second fit in the new wind: estimate from {w2['from']:.0f} at {w2['speed']:.1f} is {err2:.2f} m/s "
                       f"from the true wind (from 90 at 5); an obsolete estimate still pulling it? (F2)")
        notes.append(f"second fit in the new wind: {err2:.2f} m/s from the true wind")

        w3 = next_fit(v, "third fit in the new wind", timeout=90)
        if wind_err(w3, b_vec) > 0.6:
            raise Fail(f"third fit in the new wind drifted: from {w3['from']:.0f} at {w3['speed']:.1f}")
        notes.append("third fit stays on it")
    finally:
        v.sim(turn=0)
    return "; ".join(notes)


# ---- the landing circuit ----------------------------------------------------------------------
# A scripted pilot flies the simulated aircraft round the whole circuit, the way a pilot would: it flies
# the leg the vario has called, turns when the vario says "turn now", and follows the last leg to the field.
# The circuit's geometry is worked out here, independently, from the settings and the wind (the frame in
# Approach.h: origin at the landing zone, x into wind, y to the right), and every call the vario makes is
# checked against it: the order of the legs, the turn directions, where along each leg each call comes, the
# base turn point from the height at that moment, no leg started early, no steering call for a pilot on course.

LZ_LAT, LZ_LON = 51.5, 0.5
AIR_MS = 38.0 / 3.6  # the firmware's trim airspeed; the vario assumes it when the wind is entered by hand


def to_frame(lat, lon, up):
    e = (lon - LZ_LON) * math.radians(1) * 6371000.0 * math.cos(math.radians(LZ_LAT))
    n = (lat - LZ_LAT) * math.radians(1) * 6371000.0
    u = math.radians(up)
    return e * math.sin(u) + n * math.cos(u), e * math.cos(u) - n * math.sin(u)


def ground_glide(cfg, up, wfrom, wspd, track):
    """Wind.groundGlide: engine-off glide ratio over the ground along a track."""
    along = -wspd * math.cos(math.radians(track - wfrom))
    return cfg["glideRatio"] * max(AIR_MS + along, 1.0) / AIR_MS


class Pilot:
    """The scripted pilot and the recording of what the vario said."""

    def __init__(self, v, cfg, up, wspd, side, sink):
        self.v, self.cfg, self.up, self.wspd, self.side, self.sink = v, cfg, up, wspd, side, sink
        self.S = cfg["crosswindLegM"]
        self.legs = []     # (phase from, phase to, x, y, agl, ground speed at the time)
        self.calls = []    # (kind, x, y) of each turn call
        self.call_legs = []  # the leg named after each turn call ("turn right now, downwind"), None when there was none
        self.learned = []
        self.says = []     # (time, text) of everything spoken
        self.steer = []    # steering calls
        self.phase_t = []  # device time of each leg change
        self.pos = (0.0, 0.0)
        self.mark = v.mark()
        self.seen = self.mark
        self.target = None
        self.touchdown = None
        self.t0 = time.time()

    # ---- what the vario said since last time
    def listen(self):
        self.v.poll()
        new = self.v.lines[self.seen:]
        self.seen = len(self.v.lines)
        out = []
        for l in new:
            if l.tag == "SAY":
                self.says.append((l.t, l.msg))
                mm = re.search(r"(turn_(right|left)_now|turn_crosswind_now)(?: (upwind|crosswind|downwind|base|final))?", l.msg)
                if mm:
                    self.calls.append((mm.group(1), self.pos[0], self.pos[1]))
                    self.call_legs.append(mm.group(3))
                    out.append(("call", mm.group(1)))
                if re.search(r"steer_(right|left)", l.msg):
                    self.steer.append((l.t, l.msg, self.pos))
                    out.append(("steer", l.msg))
            elif l.tag == "APR":
                ml = re.match(r"Pilot's turn (\d): (\d+) deg/s, reaction ([\d.]+) s; lead now ([\d.]+) s", l.msg)
                if ml:
                    self.learned.append((int(ml.group(1)), float(ml.group(2)), float(ml.group(3)), float(ml.group(4))))
                mm = re.match(r"(.+) -> (.+) at x=(-?\d+) y=(-?\d+) agl=(-?\d+) side=(-?\d)", l.msg)
                if mm:
                    self.phase_t.append(l.t)
                    gs = self.state["speed"] if hasattr(self, "state") else 0
                    self.legs.append((mm.group(1), mm.group(2), int(mm.group(3)), int(mm.group(4)), int(mm.group(5)), gs))
                    out.append(("phase", mm.group(2)))
        return out

    def heading_for(self, track, wfrom, wspd):
        """Heading that makes good a track in the wind (crab angle)."""
        wt = math.radians(wfrom + 180.0)
        we, wn = wspd * math.sin(wt), wspd * math.cos(wt)
        t = math.radians(track)
        wr = we * math.cos(t) - wn * math.sin(t)  # wind component to the right of the track
        wr = max(-AIR_MS * 0.9, min(AIR_MS * 0.9, wr))
        return (track - math.degrees(math.asin(wr / AIR_MS))) % 360.0

    def fly_line(self, px, py, rel, wfrom, wspd):
        x, y = self.pos
        a = math.radians(rel)
        right = -(x - px) * math.sin(a) + (y - py) * math.cos(a)
        desired = (self.up + rel - math.degrees(math.atan2(right, 150.0))) % 360.0
        return desired

    def fly_to(self, tx, ty):
        x, y = self.pos
        return (self.up + math.degrees(math.atan2(ty - y, tx - x))) % 360.0

    def step(self, wfrom, wspd):
        self.state = st = self.v.get_json("/api/sim")
        self.pos = to_frame(st["lat"], st["lon"], self.up)
        self.agl = st["alt"] - GROUND_M
        return st


def run_circuit(v, cfg, up, wspd, side, sink, h0, events=None, wind_change=None, ignore_first_call_s=0.0,
                wrong_way_s=0.0, gps_gap=None, label="", turn_rate=25):
    """Flies one circuit. Returns the Pilot with everything it recorded. `events` is unused hook space."""
    S, ld_s = cfg["crosswindLegM"], cfg["turnLeadS"]
    pl = Pilot(v, cfg, up, wspd, side, sink)
    wfrom = up
    # takeoff into wind, climb, level at h0; the circuit starts back downwind of the field, with the tailwind
    airborne(v, hdg=up, air=AIR_MS, wfrom=up, wspd=wspd, climb=h0, vz=5.0)
    v.sim(turnRate=turn_rate)
    pl.mark = v.mark()
    pl.seen = pl.mark
    pl.step(wfrom, wspd)
    v.action("approach")
    t_start = time.time()
    # geometry the vario should have worked out (windManual, so the numbers are exactly known)
    gg_up = ground_glide(cfg, up, wfrom, wspd, up)
    final_len = max(cfg["finalHeightM"] * gg_up, cfg["minFinalM"])
    x_cross = -final_len + cfg["downwindLegM"]
    x_start = x_cross - cfg["upwindLegM"]
    pl.geom = dict(final_len=final_len, x_cross=x_cross, x_start=x_start)
    phase = "to start"
    wrong_until = time.time() + wrong_way_s
    ignore_until = None      # overshoot: the pilot does not turn when told to, until this time
    descending = False
    gap_done = gps_gap is None
    gap_end = None
    changed = wind_change is None
    while True:
        if time.time() - t_start > 420:
            raise Fail(f"{label}circuit not finished in 7 minutes: phase {phase}, position {pl.pos[0]:.0f},{pl.pos[1]:.0f}, "
                       f"{pl.agl:.0f} m up; legs so far {[a_[1] for a_ in pl.legs]}")
        pl.step(wfrom, wspd)
        for kind, what in pl.listen():
            if kind == "phase":
                phase = what
                if what == "downwind":  # engine off from here: the pilot sinks at the glide the vario assumes
                    v.sim(vz=-AIR_MS / cfg["glideRatio"])
                if what == "crosswind" and ignore_first_call_s and ignore_until is None:
                    ignore_until = time.time() + ignore_first_call_s
                    pl.ignored_at = pl.pos
        if not descending:
            v.sim(vz=-sink)
            descending = True
        eff = phase
        if phase == "crosswind" and ignore_until and time.time() < ignore_until:
            eff = "upwind"  # not turned yet
        if wrong_until > time.time() and phase == "to start":
            # flies away from the start point on purpose: the vario must keep telling it
            v.sim(turnTo=pl.heading_for((up + 180 + 90) % 360, wfrom, wspd))
            time.sleep(0.4)
            continue
        if eff == "to start":
            track = pl.fly_to(x_start, 0.0)
        elif eff == "upwind":
            track = pl.fly_line(x_start, 0.0, 0.0, wfrom, wspd)
        elif eff == "crosswind":
            track = pl.fly_line(x_cross, 0.0, 90.0 * side, wfrom, wspd)
        elif eff == "downwind":
            track = pl.fly_line(x_cross, side * S, 180.0, wfrom, wspd)
        elif eff == "base":
            bx = next((l[2] for l in pl.legs if l[1] == "base"), pl.pos[0])
            track = pl.fly_line(bx, side * S, -90.0 * side, wfrom, wspd)
        else:
            track = pl.fly_to(0.0, 0.0)
        if phase == "upwind" and not gap_done and pl.pos[0] > x_start + 100:
            v.sim(gps="silent")
            gap_end = time.time() + gps_gap
            pl.gap_legs = len(pl.legs)
            gap_done = True
        if gap_end and time.time() > gap_end:
            v.sim(gps="fix")
            gap_end = None
            pl.gap_legs_after = len(pl.legs)
        if not changed and phase == "downwind":
            v.sim(windSpd=wind_change)
            v.set(windSpeedMs=wind_change)
            wspd = wind_change
            pl.wind_changed_at = pl.pos
            changed = True
        v.sim(turnTo=pl.heading_for(track, wfrom, wspd))
        if pl.agl <= 2.0 or (phase == "final" and pl.pos[0] > 40):
            pl.touchdown = pl.pos
            break
        time.sleep(0.35)
    v.sim(vz=0, speed=0, fly=0)
    pl.wspd_final = wspd
    return pl


def check_circuit(pl, cfg, up, wfrom, wspd, side, label, allow_steer=False, x_tol=45):
    """The vario's calls against the geometry. Raises Fail with the first thing that is wrong."""
    S = cfg["crosswindLegM"]
    order = ["to start->upwind", "upwind->crosswind", "crosswind->downwind", "downwind->base", "base->final"]
    pl.legs = [l for l in pl.legs if l[0] != "off"]
    got = [f"{a}->{b}" for a, b, *_ in pl.legs]
    if got != order:
        raise Fail(f"{label}legs came as {got}, expected {order}")
    ld = lambda gs: max(gs, 5.0) * cfg["turnLeadS"]
    notes = []
    # turn directions
    want_dir = "turn_right_now" if side > 0 else "turn_left_now"
    kinds = [c[0] for c in pl.calls]
    # the pilot flew on course: the "to start -> upwind" turn call is only made when off heading
    turns = [k for k in kinds]
    if len(turns) > 4:  # the first one aligns the pilot with the upwind leg on arrival at the start (either way)
        turns = turns[-4:]
    if side:
        bad = [k for k in turns if k != want_dir]
        if bad:
            raise Fail(f"{label}turn calls {turns}: expected only {want_dir} for a {'right' if side > 0 else 'left'}-hand circuit")
    if len(turns) < 4:
        raise Fail(f"{label}only {len(turns)} turn calls in a whole circuit: {kinds}")
    # each turn call names the leg that starts ("turn right now, downwind"): the last four are the circuit's four turns
    if pl.call_legs[-4:] != ["crosswind", "downwind", "base", "final"]:
        raise Fail(f"{label}the leg named with each turn call: {pl.call_legs}, expected the last four to be crosswind, downwind, base, final")
    g = pl.geom
    # upwind -> crosswind
    _, _, x, y, agl, gs = pl.legs[1]
    lead = ld(gs)
    if not (g["x_cross"] - lead - 30 <= x <= g["x_cross"] - lead + 30) or abs(y) > 40:
        raise Fail(f"{label}crosswind turn called at x={x} y={y}, expected x about {g['x_cross'] - lead:.0f} (cross point {g['x_cross']:.0f} less lead {lead:.0f}), y about 0")
    notes.append(f"crosswind turn at x={x} (expected {g['x_cross'] - lead:.0f})")
    # crosswind -> downwind
    _, _, x, y, agl, gs = pl.legs[2]
    lead = ld(gs)
    if not (S - lead - 30 <= side * y <= S - lead + 30) or abs(x - g["x_cross"]) > x_tol:
        raise Fail(f"{label}downwind turn called at x={x} y={y}, expected y about {side * (S - lead):.0f}, x about {g['x_cross']:.0f}")
    notes.append(f"downwind turn at y={y} (expected {side * (S - lead):.0f})")
    # downwind -> base: from the height at that moment
    _, _, x, y, agl, gs = pl.legs[3]
    lead = ld(gs)
    wnow = pl.wspd_final
    gg_base = ground_glide(cfg, up, wfrom, wnow, (up - 90 * side) % 360)
    gg_up = ground_glide(cfg, up, wfrom, wnow, up)
    h_final = agl - S / gg_base
    xb = h_final * gg_up
    max_xb = (g["final_len"]) + cfg["downwindLegM"]
    xb_c = min(max(xb, cfg["minFinalM"]), max_xb)
    if abs(x - (-xb_c + lead)) > 45:
        raise Fail(f"{label}base turn called at x={x} with {agl} m height: expected x about {-xb_c + lead:.0f} "
                   f"(final {xb_c:.0f} m from the field from {h_final:.0f} m at the end of base, glide {gg_up:.1f})")
    notes.append(f"base turn at x={x} for {agl} m (expected {-xb_c + lead:.0f}; final {xb_c:.0f} m)")
    # base -> final
    _, _, x, y, agl, gs = pl.legs[4]
    lead = ld(gs)
    need = math.hypot(x, y) / ground_glide(cfg, up, wfrom, wnow, (up + math.degrees(math.atan2(-y, -x))) % 360)
    if not (side * y <= lead + 45 or agl < need * 1.05 + 3):
        raise Fail(f"{label}final turn called early at y={y} (lead {lead:.0f}) with {agl} m where {need:.0f} m is needed")
    notes.append(f"final turn at y={y}, {agl} m up")
    # touchdown near the field
    if pl.touchdown is None:
        raise Fail(f"{label}never reached the ground")
    miss = math.hypot(*pl.touchdown)
    notes.append(f"touchdown {miss:.0f} m from the field")
    # Steering calls: none for a pilot who is on the line or who is making the turn that was just called (F6:
    # a pilot who reacts after a few seconds and then turns for 4 to 8 s was told "steer" in the middle of it).
    if not allow_steer and pl.steer:
        raise Fail(f"{label}steering calls to a pilot who is on the line or making a called turn: "
                   f"{[(round(t), m, round(p_[0]), round(p_[1])) for t, m, p_ in pl.steer]}; legs {pl.legs}")
    notes.append("no steering call during any called turn" if not pl.steer else f"{len(pl.steer)} steering calls (a turn was ignored on purpose)")
    return "; ".join(notes)


def circuit_settings(v, up, wspd, side):
    v.set(lzMode=1, lzLat=LZ_LAT, lzLon=LZ_LON, lzElevM=GROUND_M, windManual=True, windFromDeg=up, windSpeedMs=wspd,
          circuitSide=2 if side > 0 else 1, glideRatio=5.0, trimAirspeedMs=AIR_MS, circuitHeightM=121.92,
          upwindLegM=400.0, crosswindLegM=400.0, downwindLegM=400.0, finalHeightM=50.0, minFinalM=100.0,
          arrivalRadiusM=80.0, turnLeadS=3.0, offHeadingDeg=30.0, guidanceUpdateS=20)


def circuit_restore(v):
    v.sim(turn=0, fly=0, speed=0, vz=0, gps="silent")
    v.set(lzMode=TEST_SETTINGS["lzMode"], lzLat=0.0, lzLon=0.0, lzElevM=0.0, windManual=False, windFromDeg=0.0,
          windSpeedMs=0.0, circuitSide=0, turnLearn=True)


def t_circuit_right(v, a):
    """The whole landing circuit, right-hand, in a steady 4 m/s wind from 250 degrees: a scripted pilot flies
    from the first press of the button to the ground, following the vario's calls (see run_circuit)."""
    up, wspd, side = 250.0, 4.0, 1
    try:
        circuit_settings(v, up, wspd, side)
        pl = run_circuit(v, v.settings(), up, wspd, side, sink=0.5, h0=280.0, label="right-hand: ")
        return check_circuit(pl, v.settings(), up, up, wspd, side, "right-hand: ")
    finally:
        circuit_restore(v)


def t_circuit_slow_pilot(v, a):
    """A pilot who turns at 6 deg/s (15 s for a quarter circle): the vario measures the first two turns, says so in its log,
    and calls the base turn earlier than the setting's 3 s would (the later turns are called to suit the pilot)."""
    up, wspd, side = 250.0, 4.0, 1
    try:
        circuit_settings(v, up, wspd, side)
        cfg = v.settings()
        pl = run_circuit(v, cfg, up, wspd, side, sink=0.5, h0=300.0, label="slow pilot: ", turn_rate=6)
        order = ["to start->upwind", "upwind->crosswind", "crosswind->downwind", "downwind->base", "base->final"]
        legs = [l for l in pl.legs if l[0] != "off"]
        got = [f"{a_}->{b}" for a_, b, *_ in legs]
        if got != order:
            raise Fail(f"slow pilot: legs came as {got}, expected {order}")
        if len(pl.learned) < 2:
            raise Fail(f"slow pilot: the vario measured {len(pl.learned)} turns, expected 2 (log lines: {pl.learned})")
        rates = [x[1] for x in pl.learned[:2]]
        if not all(3.5 <= x <= 9.0 for x in rates):
            raise Fail(f"slow pilot: measured turn rates {rates} deg/s for a 6 deg/s pilot")
        lead_s = pl.learned[1][3]
        if lead_s < 8.0:
            raise Fail(f"slow pilot: learned lead {lead_s} s, expected at least 8 s for a 15 s quarter turn")
        # base turn point against what the setting's lead would give
        _, _, x, y, agl, gs = legs[3]
        gg_base = ground_glide(cfg, up, up, wspd, (up - 90 * side) % 360)
        gg_up = ground_glide(cfg, up, up, wspd, up)
        xb = max((agl - cfg["crosswindLegM"] / gg_base) * gg_up, cfg["minFinalM"])
        early = x - (-xb + max(gs, 5.0) * cfg["turnLeadS"])  # the leg runs towards -x, so a turn called earlier is at a larger x
        if early < 20:
            raise Fail(f"slow pilot: base turn called at x={x}, only {early:.0f} m earlier than with the setting's lead")
        return f"measured {pl.learned[0][1:]} and {pl.learned[1][1:]}; lead {lead_s:.1f} s; base turn {early:.0f} m earlier than the setting would call it"
    finally:
        circuit_restore(v)


def t_circuit_left_wind(v, a):
    """A left-hand circuit in a 6 m/s wind from 100 degrees that eases to 3 m/s as the pilot turns downwind
    (the wind setting is changed with it): the legs are the same, the turns are to the left, and the base turn
    point is worked out from the wind at that moment."""
    up, wspd, side = 100.0, 6.0, -1
    try:
        circuit_settings(v, up, wspd, side)
        pl = run_circuit(v, v.settings(), up, wspd, side, sink=0.5, h0=300.0, wind_change=3.0, label="left-hand: ")
        return check_circuit(pl, v.settings(), up, up, wspd, side, "left-hand: ")
    finally:
        circuit_restore(v)


def t_circuit_events(v, a):
    """A right-hand circuit that goes wrong on the way, and the vario's guidance has to cope: the pilot first
    flies away from the start point (the vario must say so again and not start the first leg), the GPS drops
    out for 6 s on the upwind leg (no leg may start or end while there is no position), and the pilot ignores
    the first turn call and flies on for 12 s (the vario must not start the next leg, must steer the pilot
    round, and the circuit must still be completed in order)."""
    up, wspd, side = 250.0, 4.0, 1
    try:
        circuit_settings(v, up, wspd, side)
        v.set(turnLearn=False)  # this pilot ignores the first call on purpose: a learned lead would shift the later turns
        pl = run_circuit(v, v.settings(), up, wspd, side, sink=0.5, h0=300.0, wrong_way_s=14.0, gps_gap=6.0,
                         ignore_first_call_s=12.0, label="events: ")
        notes = check_circuit(pl, v.settings(), up, up, wspd, side, "events: ", allow_steer=True, x_tol=250)
        # wrong way: guidance repeated before the first leg started
        first_leg_t = pl.phase_t[1]  # [0] is "off -> to start", the press of the button
        starts = [t for t, m in pl.says if "circuit_start" in m and t < first_leg_t]
        if len(starts) < 2:
            raise Fail(f"events: flying away from the start point, the start point was called {len(starts)} time(s) "
                       f"before the upwind leg began, expected at least 2: {pl.says[:6]}")
        # GPS gap: no leg change while it lasted
        if pl.gap_legs != pl.gap_legs_after:
            raise Fail(f"events: a leg change was logged while the GPS was out ({pl.gap_legs} -> {pl.gap_legs_after})")
        # overshoot: the vario steered the pilot round while the crosswind leg waited
        steers_right = [m for t, m, p_ in pl.steer if "steer_right" in m]
        if not steers_right:
            raise Fail("events: the pilot flew on past the crosswind turn for 12 s and the vario never said steer right")
        return (notes + f"; start point called {len(starts)} times while flying away, no leg change during the 6 s "
                        f"GPS loss, {len(steers_right)} steer right calls while the turn was ignored")
    finally:
        circuit_restore(v)


# ---- sensor fault injection ---------------------------------------------------------------------
# Designed behaviour, from the code (docs/TEST_FINDINGS.md has what is not handled):
#   barometer: no sample for 2 s, or a pressure outside 20000..110000 Pa, means "Barometer not working"
#     (banner, vario tones off); samples returning restart the filter, the vario settles again, no reboot.
#     Plausibility checks (BaroGuard.h): the same reading for baroStuckS (5 s) = frozen = not working; one sample
#     further than an aircraft can move is held back and only accepted if the next one agrees (a persistent
#     step); NaN is impossible. Noise is not detected (it only raises the wobble of the reading).
#   GPS: no position for 5 s in flight is announced ("no GPS"); a fix needs a valid position under 2.5 s old
#     and 4 satellites. The flight goes on.
#   battery: below the low-battery voltage for 10 s one call, repeated every 5 minutes; under 2.5 V is taken
#     for USB power and nothing is announced.

def baro_state(v, want_ok, timeout, what):
    return v.wait_nav(lambda n: n["baro"] == ("1" if want_ok else "0"), timeout, what)


def baro_back(v, m):
    """After a fault: the barometer reads again, settles, and the vario tone works, without a reboot."""
    up = v.status()["uptime"]
    baro_state(v, True, 8, "barometer ok again")
    v.expect("BARO", r"Vario settled", m, timeout=25, what="vario settled again after the fault")
    if v.status()["uptime"] < up:
        raise Fail("the vario restarted")


@no_baro_takeoff
def t_fault_baro(v, a):
    """Barometer faults: no samples, impossible pressure, NaN, a frozen value (all: detected, banner, tones off,
    recovery without a reboot), one wild sample (held back: no tone, no reading), persistent steps (accepted once,
    no tone), intermittent dropouts, and noise (not a fault: the reading's wobble is bounded)."""
    notes = []
    reset(v, baro="on", alt=GROUND_M + 300, vz=0)
    v.sim(hasGps=0)  # the ready check names the first thing wrong; with a GPS module that would be the GPS
    time.sleep(7)
    baro_state(v, True, 8, "barometer ok at the start")
    try:
        for fault in ("dead", "range"):
            m = v.mark()
            v.sim(baroFault=fault, vz=2.0)  # climbing, with a sensor that cannot see it
            baro_state(v, False, 8, f"barometer not ok with '{fault}'")
            if not v.status()["flying"]:  # the banner is the ready check, which only runs on the ground
                v.expect("RDY", r"Barometer not working", m, timeout=8, what="banner")
            if v.status()["baro"]["ok"]:
                raise Fail("status still reports the barometer ok")
            silent_for(v, 4.0, f"barometer {fault}")
            m = v.mark()
            v.sim(baroFault="none", vz=0)
            baro_back(v, m)
            m = v.mark()
            v.sim(vz=2.0)
            v.expect("TONE", r"^climb", m, timeout=16, what="climb tone after recovery")
            v.sim(vz=0)
            v.expect("TONE", r"^off", m, timeout=10, what="tone off")
            time.sleep(1)
            notes.append(f"{fault}: detected, banner, silent, recovered")

        # impossible value that is not a number
        m = v.mark()
        v.sim(baroFault="nan", vz=2.0)
        baro_state(v, False, 8, "barometer not ok with a NaN pressure")
        silent_for(v, 3.0, "NaN pressure")
        m = v.mark()
        v.sim(baroFault="none", vz=0)
        baro_back(v, m)
        notes.append("NaN pressure: detected, silent, recovered")

        # frozen value (F7): the same reading for baroStuckS (5 s) is a dead sensor, not level flight
        m = v.mark()
        v.sim(baroFault="stuck", vz=2.0)
        baro_state(v, False, 5 + 6, "barometer not ok with a frozen reading")
        if not v.status()["baro"]["frozen"]:
            raise Fail("frozen reading: baro not ok but not reported as frozen")
        v.expect("BARO", r"frozen", m, timeout=3, what="frozen reading logged")
        if not v.status()["flying"]:
            v.expect("RDY", r"Barometer not working", m, timeout=8, what="banner")
        silent_for(v, 3.0, "frozen barometer")
        m = v.mark()
        v.sim(baroFault="none", vz=0)
        baro_back(v, m)
        if v.status()["baro"]["frozen"]:
            raise Fail("still reported frozen after the reading moves again")
        notes.append("frozen value: detected after 5 s, banner, silent, recovered")

        # one wild sample (F7): held back, the vario never sees it
        reset(v, baro="on", alt=GROUND_M + 300, vz=0)
        time.sleep(7)
        rej0 = v.status()["baro"]["rej"]
        m = v.mark()
        v.sim(baroSpike=30.0)
        seen = []
        for _ in range(12):
            time.sleep(0.5)
            seen.append(abs(float(v.nav()["vs"])))
        spurious = [l for l in v.lines[m:] if l.tag == "TONE" and re.match(r"(climb|sink)", l.msg)]
        if spurious or max(seen) > 0.15:
            raise Fail(f"a single 30 m wild sample reached the vario: {len(spurious)} tone change(s), readings up to {max(seen):.2f} m/s")
        if v.status()["baro"]["rej"] <= rej0:
            raise Fail("the wild sample was not counted as rejected")
        notes.append("one 30 m wild sample: held back, no tone, vario unmoved")
        for spike in (-30.0, 12.0):  # the other way, and a smaller one
            m = v.mark()
            v.sim(baroSpike=spike)
            time.sleep(4)
            if [l for l in v.lines[m:] if l.tag == "TONE" and re.match(r"(climb|sink)", l.msg)] or abs(float(v.nav()["vs"])) > 0.15:
                raise Fail(f"a single {spike:+.0f} m sample reached the vario")
        notes.append("-30 m and +12 m single samples: held back too")

        # a persistent step (two samples agree, or the level stays moved): accepted once, the filter restarts there,
        # and the vario does not sound a climb for the 25 m step
        for what, apply, undo in (("two wild samples in a row", lambda: v.sim(baroSpikeN=2, baroSpike=30.0), None),
                                  ("a 25 m step that stays", lambda: v.sim(baroStep=25.0), lambda: v.sim(baroStep=0.0))):
            jumps0 = v.status()["baro"]["jumps"]
            m = v.mark()
            apply()
            time.sleep(1.5)
            if v.status()["baro"]["jumps"] <= jumps0:
                raise Fail(f"{what}: not recognised as a step")
            v.expect("BARO", r"stepped from", m, timeout=3, what=f"{what}: logged")
            time.sleep(3)
            if abs(float(v.nav()["vs"])) > 0.3 or [l for l in v.lines[m:] if l.tag == "TONE" and re.match(r"(climb|sink)", l.msg)]:
                raise Fail(f"{what}: the vario reacted to the step")
            if undo:
                undo()
                time.sleep(4)
            v.sim(baroSpikeN=1)
            notes.append(f"{what}: accepted as a step, no tone")

        # intermittent: dropouts shorter than 2 s go unnoticed, longer ones are detected and recovered from
        reset(v, baro="on", alt=GROUND_M + 300, vz=0)
        time.sleep(7)
        m = v.mark()
        for _ in range(8):  # 1.2 s with no samples, timed by the vario itself (an HTTP round trip is not precise enough)
            v.sim(baroDropMs=1200)
            time.sleep(2.5)
        if v.nav()["baro"] != "1" or v.find("BARO", r"stopped responding|no usable", m):
            raise Fail("dropouts of 1.2 s were reported as a barometer failure")
        silent_for(v, 2.0, "intermittent barometer")
        for i in range(3):
            m = v.mark()
            v.sim(baroFault="dead")
            baro_state(v, False, 8, f"barometer not ok in dropout {i + 1}")
            v.sim(baroFault="none")
            baro_back(v, m)
        notes.append("intermittent: 1.2 s dropouts ignored, 3 s dropouts detected and recovered x3")

        # noise
        v.sim(baroFault="noise", baroNoise=0.4)
        time.sleep(4)
        vs = [float(v.nav()["vs"]) for _ in range(8)]
        if max(abs(x) for x in vs) > 0.8:
            raise Fail(f"0.4 m of barometer noise gives vario readings {vs}")
        notes.append(f"0.4 m noise: vario reads within +-{max(abs(x) for x in vs):.2f} m/s")
    finally:
        v.sim(baroFault="none", baroNoise=0, hasGps=1)
    return "; ".join(notes)


def t_fault_gps(v, a):
    """GPS faults in flight: a dropout is announced after 5 s and the flight goes on, the position comes back
    without a reboot, a bad fix (too few satellites) is not used, a weak fix (F8) and impossible coordinates (F5) do not
    end the flight and the coordinates are not taken for a fix."""
    notes = []
    try:
        # position 0, 0 (what a module with no position often reports) is no fix. On the ground: in the air the
        # simulated aircraft would fly on from it and leave 0, 0 at once.
        on_ground_with_fix(v)
        v.sim(lat=0.0, lon=0.0)
        v.wait_nav(lambda n: n["fix"] == "0", 6, "no fix with position 0, 0")
        v.sim(lat=51.5, lon=0.5)
        v.wait_nav(lambda n: n["fix"] == "1", 10, "fix back after position 0, 0")
        notes.append("position 0, 0 on the ground: no fix, back after")
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        st = v.status()
        if not st["flying"]:
            raise Fail("not flying after takeoff")
        # dropout
        m = v.mark()
        v.sim(gps="silent")
        v.expect("SAY", r"no_gps", m, timeout=9, what="GPS loss announced after 5 s")
        time.sleep(1)
        if not v.status()["flying"]:
            raise Fail("the flight was ended by a 6 s GPS dropout")
        if v.status()["gps"]["fix"]:
            raise Fail("status still shows a fix with the GPS silent")
        # approach refused without a position, wind known from the takeoff
        m = v.mark()
        v.action("approach")
        v.expect("SAY", r"\[urgent\] no_gps$", m, timeout=4, what="approach refused without a position")
        # back
        m = v.mark()
        v.sim(gps="fix")
        v.wait_nav(lambda n: n["fix"] == "1", 8, "fix back after the dropout")
        notes.append("dropout: announced after 5 s, flight kept, approach refused, fix back without reboot")

        # too few satellites in flight: no fix, nothing from it is used
        m = v.mark()
        v.sim(sats=3)
        v.wait_nav(lambda n: n["fix"] == "0", 6, "no fix with 3 satellites")
        if not v.status()["flying"]:
            raise Fail("3 satellites for a few seconds ended the flight")
        v.sim(sats=10)
        v.wait_nav(lambda n: n["fix"] == "1", 8, "fix back with 10 satellites")
        notes.append("3 satellites: no fix; back with 10")

        # weak fix in flight (4 satellites, hdop 4.2): the speed from it is not trusted and reads zero. The flight
        # must go on (F8: the zero speed used to be taken for a landing after the landing time)
        v.sim(sats=4, hdop=4.2)
        time.sleep(14)
        if not v.status()["flying"]:
            raise Fail("a weak GPS fix (4 satellites, hdop 4.2) for 14 s in level flight ended the flight (F8)")
        notes.append("weak fix (4 satellites, hdop 4.2) for 14 s in flight: flight kept")
        v.sim(sats=10, hdop=0.9)

        # impossible coordinates (F5): not a fix, nothing from them is used, the flight goes on, recovery follows
        for what, bad in (("latitude 95", dict(lat=95.0)), ("latitude -91", dict(lat=-91.0)),
                          ("longitude 200", dict(lon=200.0))):
            v.sim(**bad)
            v.wait_nav(lambda n: n["fix"] == "0", 6, f"no fix with {what}")
            if not v.status()["flying"]:
                raise Fail(f"{what} ended the flight")
            v.sim(lat=51.5, lon=0.5)
            v.wait_nav(lambda n: n["fix"] == "1", 10, f"fix back after {what}")
        notes.append("latitude 95 / -91 and longitude 200: no fix each time, flight kept, fix back")
    finally:
        v.sim(gps="fix", sats=10, hdop=0.9, lat=51.5)
    return "; ".join(notes)


# ---- flight state against GPS quality (F1, F8) -------------------------------------------------
# Designed behaviour: a landing needs evidence that the aircraft is standing on the ground. With a trusted GPS
# ground speed that is the speed. With no fix, a weak fix (few satellites / poor HDOP) or stale data the speed
# says nothing, so it takes a steady barometer AND a height above the landing zone under
# landingNoSpeedAglM (15 m), for twice the landing time. Level or gently climbing/sinking flight at height
# is never a landing, however long the GPS is gone.

def gps_quality_case(v, what, fault, vz, hold, notes):
    m = v.mark()
    v.sim(**fault)
    v.sim(vz=vz)
    end = time.time() + hold
    while time.time() < end:
        time.sleep(2)
        if not v.status()["flying"]:
            raise Fail(f"{what}: the flight was ended within {hold - (end - time.time()):.0f} s with no landing "
                       f"(agl {v.nav()['agl']} m)")
    n = v.nav()
    v.sim(gps="fix", sats=10, hdop=0.9, vz=0)
    v.wait_nav(lambda n: n["fix"] == "1" and n["fly"] == "1", 12, f"{what}: fix back, still flying")
    notes.append(what)


def t_landing_gps_quality(v, a):
    """A bad or missing GPS must not end a flight, and a real landing is still found without it: level and
    gently climbing or sinking flight at 60 and 200 m with no GPS, a weak fix, too few satellites and
    stale data, for several times the landing time; a long loss (70 s); recovery; then real landings on the
    barometer alone, with no GPS and with a weak fix."""
    notes = []
    land_s = TEST_SETTINGS["landingTimeS"]
    hold = 2 * land_s * 2.5 + 4  # 29 s: well over twice the landing time
    weak = dict(gps="fix", sats=4, hdop=4.2)
    try:
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        for name, fault in (("no GPS", dict(gps="silent")), ("weak fix", weak),
                            ("too few satellites", dict(gps="fix", sats=3, hdop=0.9)),
                            ("no fix", dict(gps="nofix"))):
            gps_quality_case(v, f"{name}, level at 60 m, {hold:.0f} s", fault, 0.0, hold, notes)
        # higher and with a little vertical speed: still not a landing
        m = v.mark()
        v.sim(vz=3.0, to=GROUND_M + 200)
        v.expect("SIM", r"reached target altitude", m, timeout=80, what="up to 200 m")
        for name, fault in (("no GPS", dict(gps="silent")), ("weak fix", weak)):
            for vz in (0.0, 0.2, -0.2):
                gps_quality_case(v, f"{name}, {vz:+.1f} m/s at about 200 m, {hold:.0f} s", fault, vz, hold, notes)
        # a long loss
        gps_quality_case(v, "no GPS for 70 s at 200 m", dict(gps="silent"), 0.0, 70, notes)
        # recovery: the position comes back and the flight is the same flight (not a second takeoff)
        m = v.mark()
        time.sleep(3)
        if v.find("FLT", r"^takeoff", m) or v.find("APP", r"Landed", m):
            raise Fail("the flight restarted or ended when the GPS came back")

        # a real landing without GPS: down to the ground, then standing still
        m = v.mark()
        v.sim(gps="silent", vz=-3.0, to=GROUND_M + 2)
        v.expect("SIM", r"reached target altitude", m, timeout=90, what="down to the ground")
        t0 = time.time()
        v.expect("APP", r"Landed after", m, timeout=2 * land_s + 12, what="landing found on the barometer alone")
        notes.append(f"landing with no GPS found {time.time() - t0:.0f} s after touchdown")

        # a real landing with a weak fix
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        m = v.mark()
        v.sim(**weak, vz=-3.0, to=GROUND_M + 2)
        v.expect("SIM", r"reached target altitude", m, timeout=60, what="down to the ground, weak fix")
        v.expect("APP", r"Landed after", m, timeout=2 * land_s + 12, what="landing found with a weak fix")
        notes.append("landing with a weak fix found")
    finally:
        v.sim(gps="fix", sats=10, hdop=0.9, vz=0)
    return "; ".join(notes[-6:]) + f" ({len(notes)} cases kept the flight)"


def t_landing_hover(v, a):
    """F10: a paramotor holding station in a strong headwind has a good GPS and a ground speed of about zero, level, at
    height. That must not be taken for a landing as quickly as a pilot standing still on the ground: near the ground
    the landing time (5 s in the tests) is enough, higher up it has to last landingHighMult (5) times as long (25 s).
    Flown here as a headwind that cancels the airspeed. With the multiplier at 1 it lands after 5 s (the old rule);
    and a real landing near the ground is still found after the normal time."""
    notes = []
    land_s = TEST_SETTINGS["landingTimeS"]
    try:
        airborne(v, hdg=90, air=10, wfrom=90, wspd=0, climb=60.0)
        m = v.mark()
        v.sim(air=4, windSpd=4)  # headwind equal to the airspeed: ground speed about 0, level, at 60 m
        v.wait_nav(lambda n: float(n["gs"]) < 0.5, 8, "ground speed down to about zero")
        time.sleep(land_s * 3 + 1)  # three times the old limit
        if not v.status()["flying"] or v.find("APP", r"Landed after", m):
            raise Fail("hovering in a headwind at 60 m with a good GPS was taken for a landing after the short time (F10)")
        notes.append(f"hovering at 60 m for {land_s * 3 + 1} s: still flying")
        v.expect("APP", r"Landed after", m, timeout=land_s * 5 + 12, what="landing declared after the long time at height")
        notes.append(f"declared landed after about {land_s * 5} s (landingHighMult 5)")

        # the multiplier is a setting: at 1 the old rule
        v.set(landingHighMult=1)
        airborne(v, hdg=90, air=10, wfrom=90, wspd=0, climb=60.0)
        m = v.mark()
        v.sim(air=4, windSpd=4)
        v.expect("APP", r"Landed after", m, timeout=land_s + 14, what="landing at height with the multiplier at 1")
        notes.append("multiplier 1: landed after the normal time, as before")
        v.set(landingHighMult=TEST_SETTINGS.get("landingHighMult", 5))

        # a landing near the ground is not slowed
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        v.sim(vz=-4.0, to=GROUND_M + 2)
        m = v.mark()
        v.expect("SIM", r"reached target altitude", m, timeout=60, what="down to the ground")
        v.sim(fly=0, speed=0)
        t0 = time.time()
        v.expect("APP", r"Landed after", m, timeout=land_s + 10, what="landing near the ground")
        notes.append(f"near the ground: landed {time.time() - t0:.0f} s after stopping")
    finally:
        v.set(landingHighMult=5)
        v.sim(air=10, windSpd=0, fly=0, speed=0)
    return "; ".join(notes)


def t_fault_battery(v, a):
    """Battery faults: a zero or impossible voltage, a flickering reading. Designed behaviour above; the
    low-battery call is made once however the reading flickers, and the vario copes with 0 V and 6 V."""
    notes = []
    try:
        reset(v, baro="on", alt=GROUND_M, vz=0)
        # flicker around the threshold: at most one call in 60 s
        m = v.mark()
        end = time.time() + 40
        low = True
        while time.time() < end:
            v.sim(batt=3.40 if low else 3.60)
            low = not low
            time.sleep(0.6)
        calls = [l for l in v.lines[m:] if l.tag == "SAY" and "battery_low" in l.msg]
        v.poll()
        calls = [l for l in v.lines[m:] if l.tag == "SAY" and "battery_low" in l.msg]
        if len(calls) > 1:
            raise Fail(f"a flickering battery reading gave {len(calls)} low-battery calls in 40 s")
        notes.append(f"flickering 3.4/3.6 V: {len(calls)} call(s) in 40 s")
        # impossible readings
        for volts in (0.0, 6.0):
            m = v.mark()
            v.sim(batt=volts)
            _, mm = v.expect("BAT", r"v=(-?[\d.]+) pct=(-?\d+)", m, timeout=6, what=f"battery trace at {volts} V")
            pct = int(mm.group(2))
            if not 0 <= pct <= 100:
                raise Fail(f"{volts} V gives {pct} % of battery")
            v.never("SAY", r"battery_low", m, 12 if volts == 0.0 else 3)
            notes.append(f"{volts} V: {pct} %, no call")
        # recovery
        m = v.mark()
        v.sim(batt=3.9)
        v.expect("BAT", r"v=3\.9", m, timeout=6, what="reading back at 3.9 V")
    finally:
        v.sim(batt="off")
    return "; ".join(notes)


def t_battery_flight(v, a):
    """Battery warnings in flight (F5): the ordinary low-battery call, the critical call (urgent, not the ordinary one),
    and a battery reading that disappears in flight after a real one was seen (urgent: the vario cannot say what is
    left), against the same 0 V on the ground, which is just USB power and stays silent. Recovery silences it again."""
    notes = []
    try:
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        v.sim(batt=3.9)
        time.sleep(3)
        m = v.mark()
        v.never("SAY", r"battery_low", m, 8)
        # low: normal priority, after 10 s
        m = v.mark()
        v.sim(batt=3.40)
        v.expect("SAY", r"\[normal\] battery_low( |$)", m, timeout=16, what="low battery call")
        notes.append("3.40 V: ordinary call")
        # critical: urgent after 5 s
        m = v.mark()
        t0 = time.time()
        v.sim(batt=3.20)
        v.expect("SAY", r"\[urgent\] battery_critical\|battery_low( |$)", m, timeout=12, what="critical battery call")
        notes.append(f"3.20 V: urgent call after {time.time() - t0:.0f} s")
        # a recovered reading is silent, and a lost one in flight is called
        m = v.mark()
        v.sim(batt=3.9)
        v.never("SAY", r"battery_low", m, 8)
        m = v.mark()
        v.sim(batt=0.0)
        v.expect("SAY", r"\[urgent\] battery_lost\|battery_low$", m, timeout=14, what="battery reading lost in flight")
        notes.append("reading lost in flight (0 V): urgent call")
        m = v.mark()
        v.sim(batt=3.9)
        v.never("SAY", r"battery_low", m, 8)
        notes.append("recovered: silent")
        # the same 0 V on the ground: USB power, silent
        m = v.mark()
        v.sim(vz=-3.0, to=GROUND_M + 2)
        v.expect("SIM", r"reached target altitude", m, timeout=60, what="down to the ground")
        v.sim(fly=0, speed=0)  # the aircraft model would go on at 10 m/s along the ground; stand still
        v.expect("APP", r"Landed after", m, timeout=30, what="landed")
        m = v.mark()
        v.sim(batt=0.0)
        v.never("SAY", r"battery_low", m, 14)
        notes.append("0 V on the ground: silent (USB power)")
    finally:
        v.sim(batt="off")
    return "; ".join(notes)


# ---- pilot interface: button, spoken status, in-flight alerts, resuming a flight ---------------------------------
# Designed behaviour (docs/User_Guide_(advanced).md, "The button"):
#   with a GPS     single = tell me (height, landing zone, wind, battery); double = landing approach start/cancel;
#                  hold 1.5 s = emergency glide start/cancel; triple = mute/unmute
#   without a GPS  single = tell me (height, battery); double, triple, hold = mute/unmute
#   every press gives a click cue; 0.8 s into a hold a "keep holding" cue
# The simulator injects decoded button events (button=press|holdcue|single|double|triple|hold); the decoding of
# real presses is checked by the `button_logic` unit suite.

def t_button_logic(v, a):
    """Software-level unit test of the button decoding (input/Button.cpp, ButtonDecoder): click counts, gap, triple at
    once, hold cue at 0.8 s and hold at 1.5 s, abandoned holds do nothing, click then hold, contact bounce."""
    n = unit_suite(v, "button", 14)
    return f"{n} cases passed: single/double/triple clicks, gloved gaps, hold with cue, abandoned hold, bounce"


def t_height_logic(v, a):
    """Software-level unit test of the height trace (flight/HeightHistory): nothing recorded on the ground, kept after
    landing, a gap marker block between flights, the ring of 300."""
    n = unit_suite(v, "height", 8)
    return f"{n} cases passed: no samples on the ground, trace kept after landing, one gap between flights, ring"


def t_status_logic(v, a):
    """Software-level unit test of the spoken status (app/Status.cpp): order and content of the two calls, clock
    positions, units, battery minutes or percent, and what is left out without a fix, a landing zone, wind or GPS."""
    n = unit_suite(v, "status", 15)
    return f"{n} cases passed: height, landing zone with clock and distance, wind, battery, units, what is left out"


def t_alerts_logic(v, a):
    """Software-level unit test of what the battery and barometer warnings say: the low-battery message with minutes,
    landing zone distance and the advice to switch off, the critical and lost-reading leads, and the barometer failed /
    back calls in flight."""
    n = unit_suite(v, "alerts", 15)
    return f"{n} cases passed: low/critical/lost battery messages, barometer failed and back"


def t_resume_logic(v, a):
    """Software-level unit test of when a flight saved in the air is resumed (flight/Resume.h): age limit, position
    reach, speed, missing time or position, order of the checks."""
    n = unit_suite(v, "resume", 20)
    return f"{n} cases passed: age, reach, flying speed, missing time/position"


def t_audio_logic(v, a):
    """Software-level unit test of what is queued for an utterance: the signal tone before urgent calls only."""
    n = unit_suite(v, "audio", 8)
    return f"{n} cases passed: urgent calls get the signal tone, nothing else does"


def factory_clicks(v, n=5):
    for _ in range(n):
        v.sim(button="press")
        time.sleep(0.35)


def t_names_default(v, a):
    """The default Wi-Fi and Bluetooth names are written into the settings, with the unit code: emptying both boxes (an empty
    name cannot be used) gives 'ParaVario-' and four hex digits in the stored values after a restart, the same code in both,
    and the status shows exactly the stored values (nothing is added behind the settings' back). A name of the owner's own
    is left as it is."""
    saved = {k: v.settings()[k] for k in ("bleName", "wifiSsid")}
    try:
        v.set(bleName="", wifiSsid="")
        restart_vario(v, "names")
        s = v.settings()
        for k in ("bleName", "wifiSsid"):
            if not re.fullmatch(r"ParaVario-[0-9A-F]{4}", s[k]):
                raise Fail(f"after emptying {k} and a restart it is {s[k]!r}, expected ParaVario-XXXX")
        if s["bleName"] != s["wifiSsid"]:
            raise Fail(f"the unit code differs: {s['bleName']} and {s['wifiSsid']}")
        w = v.status()["wifi"]
        if w["apSsid"] != s["wifiSsid"] or w["bleName"] != s["bleName"]:
            raise Fail(f"the names in use {w['apSsid']}, {w['bleName']} are not the stored {s['wifiSsid']}, {s['bleName']}")
        default = s["wifiSsid"]
        v.set(bleName="MyVario", wifiSsid="MyVario-Wifi")
        restart_vario(v, "names")
        s = v.settings()
        if s["bleName"] != "MyVario" or s["wifiSsid"] != "MyVario-Wifi" or v.status()["wifi"]["apSsid"] != "MyVario-Wifi":
            raise Fail(f"a name of the owner's own was changed: {s['bleName']!r}, {s['wifiSsid']!r}")
        return f"emptied names became {default} in the settings (same code for both, nothing added behind the settings); own names kept"
    finally:
        v.set(**saved)


def t_maint_voice(v, a):
    """With the maintenance connection on, joining the maintenance Wi-Fi at power-up is spoken once ("Connected to maintenance
    server Wi-Fi"): the vario is reached over that connection, so the line is in its log from power-up."""
    st = v.settings()
    if not (st["maintWifi"] and st["serverSsid"]):
        return "skipped: this vario is not set up for the maintenance connection"
    restart_vario(v, "maint voice")
    time.sleep(2)
    code, body = v._open("/api/debuglog?since=0")
    said = [l for l in body.decode("utf-8", "replace").splitlines() if "SAY" in l and "maint_connected" in l]
    joined = [l for l in body.decode("utf-8", "replace").splitlines() if "Joined server network" in l]
    if len(said) != 1 or len(joined) < 1:
        raise Fail(f"after a restart: {len(joined)} 'Joined' lines and {len(said)} 'Connected to maintenance server' prompts, expected 1 prompt")
    return "joined the maintenance Wi-Fi and said so once: " + said[0].strip()


def t_factory_reset(v, a):
    """The button's factory reset: five clicks give the instruction and wait; then five clicks and a sixth held for 2 s do
    the reset at the 2 s point (every setting back to its default, then a restart; the test build keeps its Wi-Fi settings
    so the harness can still reach it). Let go early = cancelled and nothing changed; not flying only; the clicks do
    nothing else meanwhile (no approach, no mute)."""
    notes = []
    try:
        on_ground_with_fix(v)
        defaults = {"speechVolume": 5, "pilotName": ""}
        v.set(speechVolume=55, pilotName="Test Pilot")

        # in flight the five clicks are nothing, and give no instruction
        airborne(v, hdg=90, air=10, wfrom=270, wspd=3, climb=40.0, vz=2.0)
        m = v.mark()
        v.sim(button="five")
        time.sleep(3)
        if v.find("SAY", r"factory_prompt", m):
            raise Fail("the factory reset prompt was given in flight")
        v.sim(vz=0, speed=0, fly=0)
        v.action("reset_flight")
        on_ground_with_fix(v)
        notes.append("no prompt in flight")

        # five clicks: the instruction, and nothing else happens with the clicks that follow
        m = v.mark()
        v.sim(button="five")
        v.expect("SAY", r"\[urgent\] factory_prompt", m, timeout=4, what="the instruction after five clicks")
        m = v.mark()
        factory_clicks(v, 5)
        st = v.status()
        if st["mode"] != "none" or st["muted"]:
            raise Fail(f"the clicks did something else: mode {st['mode']}, muted {st['muted']}")
        if v.settings()["speechVolume"] != 55:
            raise Fail("settings changed after only five clicks")
        notes.append("five clicks: instruction given, the next five clicks do nothing else")

        # the sixth press held: not at 1.6 s, at 2 s
        m = v.mark()
        v.sim(buttonHeld=1)
        v.sim(button="press")
        time.sleep(1.6)
        if v.find("APP", r"Factory reset by the button", m) or v.settings()["speechVolume"] != 55:
            raise Fail("reset before the 2 s point")
        v.expect("APP", r"Factory reset by the button", m, timeout=3, what="the reset at the 2 s point")
        v.expect("SAY", r"\[urgent\] factory_reset", m, timeout=3, what="'Restoring factory settings' spoken")
        v.sim(buttonHeld=0)
        end = time.time() + 90
        time.sleep(4)
        while True:
            try:
                json.loads(v._open("/api/status", tries=1)[1])
                break
            except Fail:
                if time.time() > end:
                    raise Fail("the vario did not come back after the factory reset")
                time.sleep(2)
        s = v.settings()
        for k, want in defaults.items():
            if s[k] != want:
                raise Fail(f"after the reset {k} = {s[k]!r}, expected the default {want!r}")
        notes.append("held 2 s: reset (settings back to defaults) and restart")

        # let go early: cancelled, nothing changed
        v.set(speechVolume=55)
        m = v.mark()
        v.sim(button="five")
        v.expect("SAY", r"factory_prompt", m, timeout=4, what="the instruction again")
        factory_clicks(v, 5)
        v.sim(buttonHeld=1)
        v.sim(button="press")
        time.sleep(0.9)
        v.sim(buttonHeld=0)
        v.expect("SAY", r"\[urgent\] factory_cancelled", m, timeout=4, what="'cancelled' after letting go early")
        if v.settings()["speechVolume"] != 55:
            raise Fail("a cancelled reset changed the settings")
        notes.append("let go early: cancelled, nothing changed")
        return "; ".join(notes)
    finally:
        v.sim(buttonHeld=0, vz=0, speed=0, fly=0)


def t_button_actions(v, a):
    """Every button gesture, injected as decoded events: click and hold cues, single = tell me (and never starts or
    cancels guidance), double = approach on/off, hold = emergency on/off, triple = mute/unmute; and the same on a vario
    with no GPS (single tells height and battery, double mutes)."""
    notes = []
    try:
        on_ground_with_fix(v)
        v.set(windManual=True, windFromDeg=270, windSpeedMs=3.0)
        m = v.mark()
        v.sim(button="press")
        v.expect("BTN", r"click", m, timeout=4, what="click cue on a press")
        v.sim(button="holdcue")
        v.expect("BTN", r"hold cue", m, timeout=4, what="hold cue")
        notes.append("click and hold cues")

        m = v.mark()
        v.sim(button="single")
        v.expect("SAY", r"^\[high\] .*landing_zone", m, timeout=4, what="single press: height and landing zone")
        v.expect("SAY", r"^\[high\] wind .*battery", m, timeout=4, what="single press: wind and battery")
        if v.status()["mode"] != "none":
            raise Fail("a single press started guidance")
        notes.append("single: tell me, starts nothing")

        m = v.mark()
        v.sim(button="double")
        v.expect("SAY", r"approach_on circuit_start", m, timeout=4, what="double press starts the approach")
        if v.status()["mode"] != "approach":
            raise Fail("double press did not start the approach")
        m = v.mark()
        v.sim(button="single")
        v.expect("SAY", r"^\[high\] .*landing_zone", m, timeout=4, what="single press during the approach still tells")
        time.sleep(1.5)
        if v.status()["mode"] != "approach":
            raise Fail("a single press cancelled the approach")
        m = v.mark()
        v.sim(button="double")
        v.expect("SAY", r"approach_off", m, timeout=4, what="double press cancels the approach")
        if v.status()["mode"] != "none":
            raise Fail("approach still on after the cancelling double press")
        notes.append("double: approach on and off; a single never cancels")

        m = v.mark()
        v.sim(button="hold")
        v.expect("SAY", r"emergency glide_range", m, timeout=4, what="hold starts the emergency glide")
        if v.status()["mode"] != "emergency":
            raise Fail("hold did not start the emergency glide")
        v.sim(button="single")
        time.sleep(1.5)
        if v.status()["mode"] != "emergency":
            raise Fail("a single press cancelled the emergency glide")
        m = v.mark()
        v.sim(button="hold")
        v.expect("SAY", r"emergency_off", m, timeout=4, what="hold cancels the emergency glide")
        notes.append("hold: emergency on and off; a single never cancels")

        m = v.mark()
        v.sim(button="triple")
        v.expect("SAY", r"vario_muted", m, timeout=4, what="triple press mutes")
        if not v.status()["muted"]:
            raise Fail("triple press did not mute")
        m = v.mark()
        v.sim(button="triple")
        v.expect("SAY", r"vario_on", m, timeout=4, what="triple press unmutes")
        notes.append("triple: mute and unmute")

        # a vario with no GPS: single tells, double mutes
        v.sim(hasGps=0)
        time.sleep(1.5)
        m = v.mark()
        v.sim(button="single")
        v.expect("SAY", r"^\[high\] ", m, timeout=4, what="single press without a GPS tells")
        v.expect("SAY", r"^\[high\] battery", m, timeout=4, what="single press without a GPS: battery")
        if v.find("SAY", r"landing_zone|wind", m):
            raise Fail("without a GPS the status spoke of the landing zone or the wind")
        m = v.mark()
        v.sim(button="double")
        v.expect("SAY", r"vario_muted", m, timeout=4, what="double press without a GPS mutes")
        m = v.mark()
        v.sim(button="hold")
        v.expect("SAY", r"vario_on", m, timeout=4, what="hold without a GPS unmutes")
        notes.append("no GPS: single tells height and battery, double/hold mute")
    finally:
        v.sim(hasGps=1)
        v.set(windManual=TEST_SETTINGS["windManual"])
        try:
            if v.status()["muted"]:
                v.sim(button="triple")
        except Fail:
            pass
    return "; ".join(notes)


def t_alerts_flight(v, a):
    """In flight: a failed barometer is spoken ("Barometer failed", urgent, then "Barometer back" when it reads again);
    and the routine height calls keep quiet while a landing approach is guiding."""
    notes = []
    try:
        airborne(v, hdg=90, air=10, wfrom=270, wspd=0, climb=60.0)
        m = v.mark()
        v.sim(baroFault="dead")
        v.expect("SAY", r"^\[urgent\] baro_failed\|~400$", m, timeout=14, what="barometer failure spoken in flight")
        m = v.mark()
        v.sim(baroFault="none")
        v.expect("SAY", r"^\[normal\] baro_back\|~800$", m, timeout=20, what="barometer back spoken")
        notes.append("barometer failed / back spoken in flight")

        # routine height calls: spoken every 10 s normally, none while the approach is on
        v.set(heightIntervalS=10, windManual=True, windFromDeg=270, windSpeedMs=3.0)
        v.sim(vz=0)
        time.sleep(3)
        m = v.mark()
        time.sleep(26)
        v.poll()
        calls = [l for l in v.lines[m:] if l.tag == "SAY" and l.msg.startswith("[low] ")]
        if len(calls) < 2:
            raise Fail(f"routine height calls every 10 s: only {len(calls)} in 26 s")
        v.action("approach")
        v.expect("SAY", r"approach_on", m, timeout=6, what="approach started")
        m = v.mark()
        time.sleep(26)
        v.poll()
        calls = [l for l in v.lines[m:] if l.tag == "SAY" and l.msg.startswith("[low] ")]
        if calls:
            raise Fail(f"routine height call during the approach: {[c.msg for c in calls]}")
        notes.append("routine height calls: 2+ in 26 s normally, none during the approach")
    finally:
        v.action("approach") if v.status()["mode"] == "approach" else None
        v.sim(baroFault="none", vz=0)
        v.set(heightIntervalS=TEST_SETTINGS.get("heightIntervalS", 0), windManual=TEST_SETTINGS["windManual"])
    return "; ".join(notes)


def restart_vario(v, what):
    """Restarts the vario and waits until it answers again; returns its uptime then."""
    before = v.status()["uptime"]
    v.action("restart")
    time.sleep(4)
    end = time.time() + 90
    while True:
        try:
            st = json.loads(v._open("/api/status", tries=1)[1])
            if st["uptime"] < before or st["uptime"] < 30:
                return st["uptime"]
        except Fail:
            pass
        if time.time() > end:
            raise Fail(f"{what}: the vario did not come back after the restart")
        time.sleep(2)


def resume_case(v, what, age_s, speed=12.0, dlat=0.0, hold_s=0, expect_resume=True, set_max=None, no_pending=False):
    """One restart in simulated flight: flies, restarts the vario, then brings the simulated sensors back `age_s` seconds
    (of GPS time) after the flight was last saved, at the same place (moved north by dlat degrees), at `speed` m/s."""
    airborne(v, hdg=90, air=10, wfrom=270, wspd=3, climb=50.0, vz=4.0)
    st = v.status()
    lat, lon, lz_before = st["gps"]["lat"], st["gps"]["lon"], st["lz"]
    t_save = st["uptime"]
    if set_max is not None:
        v.set(resumeMaxMin=set_max)
    up = restart_vario(v, what)
    m = v.mark()
    if hold_s:
        time.sleep(hold_s)  # no GPS yet
        up = v.status()["uptime"]
    base = max(0, int(t_save + age_s - up))
    v.sim(baro=1, alt=GROUND_M + 50, baroOffset=150, gps="fix", sats=10, hdop=0.9, lat=lat + dlat, lon=lon, speed=speed,
          track=90, utcBase=base)
    return m, lat, lon, lz_before


def t_resume_flight(v, a):
    """A flight saved in the air is resumed only on evidence that it is still in the air, and the vario can always get
    out of a false 'flying': switched off and on again in flight 30 minutes later at flying speed = resumed (same
    landing zone and flight time, heights right, 'Flight resumed') and then lands normally; too old, on the ground, in
    an impossible place, no GPS within 3 minutes, resume switched off, or the flight reset by the pilot = not resumed;
    and the manual reset gets a flying vario out of the state."""
    notes = []
    saved_cfg = v.settings()
    v.sim(gpsBoot="silent")  # after each restart the (real) GPS on the bench must not decide before this test has set up its flight
    # The simulated barometer jumps up 50 m a moment before the simulated GPS fix is trusted; with the setting's 'takeoff without
    # GPS' height gain that is a barometer-only takeoff, which (correctly) drops a saved flight. A real power-up in flight starts
    # from the pressure it finds, so there is no such climb: take it out of this test.
    v.set(baroTakeoffGainM=200)
    try:
        # resumed after 30 minutes at flying speed
        m, lat, lon, lz0 = resume_case(v, "resume", 1800)
        v.expect("APP", r"Resuming the flight: saved (\d+) s ago", m, timeout=20, what="the flight resumed")
        v.expect("SAY", r"flight_resumed", m, timeout=6, what="'Flight resumed' spoken")
        st = v.wait_nav(lambda n: n["fly"] == "1", 6, "flying after the resume")
        s2 = v.status()
        if not s2["flying"] or not s2["lz"]["valid"]:
            raise Fail(f"after the resume: flying {s2['flying']}, landing zone {s2['lz']}")
        if abs(s2["lz"]["lat"] - lz0["lat"]) > 1e-5 or abs(s2["lz"]["lon"] - lz0["lon"]) > 1e-5:
            raise Fail(f"the landing zone changed across the restart: {lz0} -> {s2['lz']}")
        if not 40 <= s2["agl"] <= 60:
            raise Fail(f"height after the resume is {s2['agl']} m, expected about 50 (the ground reference was restored)")
        if s2["flightTime"] < 1700:
            raise Fail(f"flight time after the resume is {s2['flightTime']} s, expected the time before plus the 30 minutes off")
        notes.append(f"30 min off at flying speed: resumed, same LZ, {s2['agl']:.0f} m, flight time {s2['flightTime']} s")
        # ... and a resumed flight lands normally: standing still ends it
        m = v.mark()
        v.sim(speed=0, vz=-4.0, to=GROUND_M + 2)
        v.expect("APP", r"Landed after", m, timeout=60, what="a resumed flight lands normally")
        notes.append("resumed flight lands normally")

        cases = (
            ("too old (4 h)", dict(age_s=4 * 3600), "the record is too old"),
            ("on the ground", dict(age_s=600, speed=0.0), "the aircraft is not moving at flying speed"),
            ("impossible place", dict(age_s=300, dlat=1.0), "the position is not where the flight could have got to"),
            ("resume switched off", dict(age_s=300, set_max=0), None),
        )
        for name, kw, why in cases:
            m, *_ = resume_case(v, name, **kw)
            if why:
                v.expect("APP", r"Saved flight not resumed: " + re.escape(why), m, timeout=20, what=f"{name}: refused")
            time.sleep(6)
            # (not "flying": the simulated aircraft is flying, so a normal takeoff follows within seconds; a resumed flight
            # has the time it was off added to its flight time, which a fresh takeoff does not)
            if v.find("APP", r"Resuming the flight", m) or v.status()["flightTime"] > 120:
                raise Fail(f"{name}: the flight was resumed")
            if name == "resume switched off":
                v.set(resumeMaxMin=saved_cfg["resumeMaxMin"])
            notes.append(f"{name}: not resumed")

        # no GPS position and time within 3 minutes: the record is dropped, and a later fix does not bring it back
        m, *_ = resume_case(v, "no GPS", age_s=300, hold_s=190)
        v.expect("APP", r"Saved flight not resumed: no GPS position and time within 3 minutes", m, timeout=30, what="gave up waiting")
        time.sleep(5)
        if v.status()["flightTime"] > 120:
            raise Fail("a flight was resumed after the 3 minutes had passed")
        notes.append("no GPS within 3 minutes: dropped")

        # the pilot's reset: a flying vario is brought down, and nothing is resumed afterwards
        airborne(v, hdg=90, air=10, wfrom=270, wspd=3, climb=50.0, vz=4.0)
        m = v.mark()
        v.action("reset_flight")
        v.expect("APP", r"Flight state reset by the pilot", m, timeout=6, what="reset")
        st = v.wait_nav(lambda n: n["fly"] == "0", 6, "not flying after the reset")
        up = restart_vario(v, "after the reset")
        m = v.mark()
        v.sim(baro=1, alt=GROUND_M + 50, baroOffset=150, gps="fix", sats=10, hdop=0.9, speed=12.0, track=90)
        time.sleep(8)
        if v.find("APP", r"waiting to be resumed|Resuming the flight", m) or v.status()["flightTime"] > 120:
            raise Fail("a flight reset by the pilot was resumed after a restart")
        notes.append("reset by the pilot: flight ended, nothing resumed after a restart")
    finally:
        v.set(resumeMaxMin=saved_cfg["resumeMaxMin"], baroTakeoffGainM=saved_cfg["baroTakeoffGainM"])
        v.sim(gpsBoot="real")
        v.sim(off=1)
    return "; ".join(notes)


# ---- memory and long-running behaviour ---------------------------------------------------------
# Criteria come from what the build measures when it is idle (below), not from numbers picked in advance:
# the lowest free memory may fall at most MEM_DROP_B further than the idle run-up, free memory after
# repeated events must come back to within MEM_LEAK_B of where it was after the first round, and no task
# may come closer than STACK_MIN_B to the end of its stack.
MEM_DROP_B = 6000
MEM_LEAK_B = 1500
STACK_MIN_B = 300


def mem_now(v, settle=3.0):
    """Free memory once things have gone quiet: the best of five readings after a pause (a single reading
    moves by several kB with whatever the web server and log are holding at that moment)."""
    time.sleep(settle)
    # Free memory steps up by about 5 kB when the network stack lets go of sockets that the requests left behind
    # (up to a minute or two): wait until it has stopped moving before taking the reading.
    recent = []
    for _ in range(30):
        recent = (recent + [v.status()["heap"]])[-4:]
        if len(recent) == 4 and max(recent) - min(recent) < 600:
            break
        time.sleep(2.0)
    best = None
    for _ in range(5):
        st = v.status()
        cur = dict(heap=st["heap"], low=st["heapMin"], block=st["heapBlock"], stack=st.get("stack", {}))
        if best is None or cur["heap"] > best["heap"]:
            best = cur
        time.sleep(0.5)
    return best


def check_stacks(m, what):
    for name, free in m["stack"].items():
        if free < STACK_MIN_B:
            raise Fail(f"{what}: task '{name}' has come within {free} B of the end of its stack (limit {STACK_MIN_B} B)")


def t_memory_events(v, a):
    """Repeated events must not leak memory or eat the stacks: settings saved, log read, the unit suites (a
    lot of String churn), flight logic reset, the vario tone changing, the microphone input swapped.
    Three rounds of each; free memory after the third may not be below the lowest of the first two by more than MEM_LEAK_B."""
    reset(v, baro="on", alt=GROUND_M + 300, vz=0)
    time.sleep(7)
    idle = mem_now(v)
    check_stacks(idle, "idle")

    def round_():
        for i in range(25):
            v.set(climbThreshold=0.3 + 0.01 * (i % 2))
        v.set(climbThreshold=TEST_SETTINGS["climbThreshold"])
        for _ in range(40):
            v._open("/api/debuglog?since=0")
        for name in ("geo", "wind", "battery"):
            for _ in range(4):
                unit_suite(v, name, 10)
        for _ in range(15):
            v.sim(reset=1)
        for i in range(12):
            v.sim(vz=3.0 if i % 2 == 0 else 0.0)
            time.sleep(0.8)
        v.sim(vz=0)
        for i in range(6):
            v.sim(micMode="synth" if i % 2 == 0 else "dead", micHz=40.0)
        v.sim(micMode="real", micHz=0)
        time.sleep(2)
        return mem_now(v)

    marks = [round_() for _ in range(3)]
    for i, m in enumerate(marks):
        check_stacks(m, f"round {i + 1}")
    # Free memory settles at one of two levels about 5 kB apart (whatever the web server and Wi-Fi hold); a leak
    # shows as each round ending lower than the lowest of the rounds before it.
    leak = min(marks[0]["heap"], marks[1]["heap"]) - marks[2]["heap"]
    if leak > MEM_LEAK_B:
        raise Fail(f"free memory after round 3 ({marks[2]['heap']} B) is {leak} B below the lowest of rounds 1 and 2 "
                   f"({marks[0]['heap']}, {marks[1]['heap']}): a leak?")
    drop = idle["low"] - marks[2]["low"]
    # The lowest-since-power-on mark is cumulative, so tests that ran earlier (a page load takes it to a few kB: F9) can
    # have set it. This test is judged on what it did itself: a fall of more than MEM_DROP_B, or a new low under 8000 B.
    new_low = marks[2]["low"] < idle["low"]
    if drop > MEM_DROP_B or (new_low and marks[2]["low"] < 8000):
        raise Fail(f"lowest free memory fell from {idle['low']} to {marks[2]['low']} B (allowed: down by {MEM_DROP_B}, "
                   f"and no new low under 8000)")
    st = marks[2]["stack"]
    return (f"free {idle['heap']} -> {[m['heap'] for m in marks]} B over three rounds, lowest {marks[2]['low']} B, largest block "
            f"{marks[2]['block']} B, stacks free {st}")


def t_soak(v, a):
    """A long simulated flight (default 10 minutes, --soak-minutes): takeoff, circles and straight legs in a 4 m/s
    wind with the vario climbing and sinking, short GPS dropouts, the IGC log running. Memory is sampled every
    10 s: after the first two minutes free memory may not trend down by more than 150 B a minute, the lowest
    free memory and the stacks stay inside the limits of t_memory_events, nothing is dropped from the log feed,
    the flight is never ended by the dropouts, and the log has about one record a second."""
    minutes = getattr(a, "soak_minutes", 10)
    airborne(v, hdg=270, air=10, wfrom=270, wspd=4, climb=200.0, vz=3.0)
    idle = mem_now(v)
    dropped0 = v.dropped
    samples = []
    t_start = time.time()
    phase_end = 0
    k = 0
    last_dropout = t_start
    v.sim(vz=0)
    while time.time() - t_start < minutes * 60:
        now = time.time()
        if now > phase_end:  # a new manoeuvre every 40 s: circle one way, the other, then straight, with climb and sink
            turn, vz = [(18, 1.5), (-18, -1.0), (0, 0.0), (18, 0.5), (-18, -0.5), (0, 1.0)][k % 6]
            if v.status()["agl"] > 330:
                vz = -abs(vz)
            if v.status()["agl"] < 80:
                vz = abs(vz) + 0.5
            v.sim(turn=turn, vz=vz)
            k += 1
            phase_end = now + 40
        if now - last_dropout > 95:
            last_dropout = now
            v.sim(gps="silent")
            time.sleep(2.0)
            v.sim(gps="fix")
        samples.append((now - t_start, mem_now(v)))
        time.sleep(10)
    v.sim(turn=0, vz=0)
    st = v.status()
    if not st["flying"]:
        raise Fail("the flight was ended during the soak (a GPS dropout of 2 s, or something else)")
    if v.dropped != dropped0:
        raise Fail(f"{v.dropped - dropped0} log lines were dropped during the soak")
    for t, m in samples:
        check_stacks(m, f"soak at {t:.0f} s")
    late = [(t, m["heap"]) for t, m in samples if t > 120]
    if len(late) >= 6:
        n = len(late)
        mx = sum(t for t, _ in late) / n
        my = sum(h for _, h in late) / n
        slope = sum((t - mx) * (h - my) for t, h in late) / max(1e-9, sum((t - mx) ** 2 for t, _ in late)) * 60.0
        if slope < -150:
            raise Fail(f"free memory trends down by {-slope:.0f} B/min over the soak (limit 150): "
                       f"{[h for _, h in late][:3]} ... {[h for _, h in late][-3:]}")
    else:
        slope = 0.0
    low = min(m["low"] for _, m in samples)
    if idle["low"] - low > MEM_DROP_B or (low < idle["low"] and low < 8000):  # judged on what the soak did itself
        raise Fail(f"lowest free memory fell from {idle['low']} to {low} B during the soak")
    w = st["wind"]
    if wind_err(w, wind_vec(270, 4.0)) > 1.5:
        raise Fail(f"after the soak the wind estimate is {w}, true wind from 270 at 4 m/s")
    return (f"{minutes} min flown, free memory {samples[0][1]['heap']} -> {samples[-1][1]['heap']} B (trend {slope:+.0f} B/min), "
            f"lowest {low} B, wind estimate from {w['from']:.0f} at {w['speed']:.1f}, no log lines dropped")


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
    v.expect("SAY", r"\] gps_fix$", m, timeout=25, what="GPS fix said after the good fix held 10 s")
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
    # level at 60 m with a steady vario for more than twice the landing time is flight, not a landing (F1)
    time.sleep(2 * TEST_SETTINGS["landingTimeS"] * 2 + 4)
    if not v.status()["flying"]:
        raise Fail("level flight at 60 m without a GPS was taken for a landing (F1)")
    m2 = v.mark()
    v.sim(vz=-3, to=GROUND_M + 2)
    v.expect("SIM", r"reached target", m2, timeout=40, what="down to the ground")
    v.expect("APP", r"Landed after", m2, timeout=20, what="landing from steady vario on the ground (2 x landing time)")
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
    v.expect("SAY", r"\[normal\] battery_low( |$)", m, timeout=10, what="low battery called after 10 s")
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
        t0 = time.time()  # the ramp starts somewhere between t0 and t_set (the HTTP round trip is not instant)
        v.set(varioVolume=100)
        t_set = time.time()
        seen = []  # (time before the request, time after the reply, volume): the device read it in between
        while time.time() - t0 < 4.6:
            tb = time.time()
            x = v.status()["vol"]["vario"]
            seen.append((tb - t0, time.time() - t0, x))
            time.sleep(0.2)
        t_set -= t0
        # 30 %/s from 10 %: about 40 % after a second, full after three. Judged against the earliest and latest the
        # reading can have been taken, so a slow reply cannot fake a jump or a crawl.
        fast = [(ta, x) for tb, ta, x in seen if x > 10 + 30.0 * ta + 6]
        if fast:
            raise Fail(f"volume rose faster than 30 %/s: {fast[0][1]} % only {fast[0][0]:.2f} s after the change was sent")
        slow = [(tb - t_set, x) for tb, ta, x in seen if tb > t_set and x < min(100.0, 10 + 30.0 * (tb - t_set)) - 12]
        if slow:
            raise Fail(f"volume rose slower than 30 %/s: {slow[0][1]} % {slow[0][0]:.2f} s after the change was applied")
        if any(b < a_ for (_, _, a_), (_, _, b) in zip(seen, seen[1:])):
            raise Fail(f"volume did not rise steadily: {[x for _, _, x in seen]}")
        near(seen[-1][2], 100, 0, "volume at the end of the ramp")
        early = max(x for tb, ta, x in seen if ta < 1.0)
        mid = [x for tb, ta, x in seen if 1.2 < ta < 1.8]
        d0 = time.time()
        v.set(varioVolume=10)
        d_set = time.time()
        time.sleep(0.8)
        tb = time.time()
        down = v.status()["vol"]["vario"]
        ta = time.time()
        # a gradual fall at 30 %/s: no faster than that since the request began, no slower than it since it was applied
        if down < 100 - 30.0 * (ta - d0) - 6 or down > max(10.0, 100 - 30.0 * (tb - d_set)) + 12:
            raise Fail(f"volume {down} % read between {tb - d_set:.1f} and {ta - d0:.1f} s after lowering the setting "
                       f"from 100 %, expected a gradual fall at 30 %/s")
        return f"10 -> 100 %: {early} % within 1 s, {mid[0] if mid else '-'} % at 1.5 s, 100 % by 4.5 s; never faster or slower than 30 %/s; falls gradually too"
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
        missing = {"baroTakeoffGainM", "varioVolume", "landingTimeS", "heightUnit", "bleRateHz", "gpsMode"} - keys
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
            if "Landing zone" in groups or "Wind" in groups or "Vario" not in groups or "GPS" not in groups:
                raise Fail(f"web page: settings groups without a GPS: {groups}")
            gps_rows = p.js("[...document.querySelectorAll('#settings details')].find(d=>d.querySelector('summary')"
                            ".firstChild.textContent==='GPS').querySelectorAll('label.row').length")
            if gps_rows != 1:
                raise Fail(f"web page: the GPS group should hold only the GPS module setting without a GPS, not {gps_rows}")
        time.sleep(2 * TEST_SETTINGS["landingTimeS"] * 2 + 4)
        if not v.status()["flying"]:
            raise Fail("level flight at 40 m without a GPS was taken for a landing (F1)")
        m = v.mark()
        v.sim(vz=-3, to=GROUND_M + 2)
        v.expect("SIM", r"reached target", m, timeout=40, what="down to the ground")
        v.expect("APP", r"Landed after", m, timeout=50, what="landing from a steady vario on the ground")
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
            p.wait_text("t-batt", r"^(~.+|\d+%|USB)$", 6)
            check_tile_layout(p, gps=True, mic=False)
            p.check_fits("flight tab without microphone")
        return f"volume back to the set 40/20 % at -20 dBFS noise, {len(keys)} settings listed, no spectrum"
    finally:
        v.sim(hasMic=1, noise="off")
        v.set(autoVolume=False, speechVolume=a.volume, varioVolume=a.volume)


def check_tile_layout(p, gps, mic, eng=False):
    """Flight tab: tiles in their groups, graph beside Height, a lone last tile full width."""
    info = p.js("""(()=>{const vis=e=>getComputedStyle(e).display!=='none';
      const ids=s=>[...document.getElementById(s).children].filter(vis).map(e=>e.querySelector('.v').id);
      const vt=document.getElementById('tile-vario'),cv=document.getElementById('hgCv');
      return {main:ids('tilesMain'),more:ids('tilesMore'),
        graphInVario:cv.parentElement===vt,
        wideMain:[...document.querySelectorAll('#tilesMain .wide')].map(e=>e.querySelector('.v').id),
        wide:[...document.querySelectorAll('#tilesMore .wide')].map(e=>e.querySelector('.v').id),
        graphPixels:(()=>{const c=document.getElementById('hgCv');if(!c.width)return -1;
          const d=c.getContext('2d').getImageData(0,0,c.width,c.height).data;let n=0;for(let i=3;i<d.length;i+=4)if(d[i])n++;return n})(),
        graphW:cv.clientWidth,tileW:vt.clientWidth}})()""")
    # Height | Flight time, Vario (with the height graph) | Ground speed, Landing zone | Wind, Glide ratio | Battery
    want_main = ["t-agl", "t-flight", "t-vario"] + (["t-speed", "t-lz", "t-wind", "t-glide"] if gps else []) + ["t-batt"]
    if info["main"] != want_main:
        raise Fail(f"web page: top tiles are {info['main']}, expected {want_main}")
    want_more = (["t-gps"] if gps else []) + (["t-rpm"] + (["t-noise"] if eng else []) if mic else [])
    if info["more"] != want_more:
        raise Fail(f"web page: lower tile group is {info['more']}, expected {want_more}")
    if not info["graphInVario"]:
        raise Fail("web page: the height graph is not inside the Vario tile")
    if len(want_main) % 2 == 1 and info["wideMain"] != [want_main[-1]]:
        raise Fail(f"web page: lone last top tile {want_main[-1]} not full width (wide: {info['wideMain']})")
    if len(want_main) % 2 == 0 and info["wideMain"]:
        raise Fail(f"web page: top tiles wrongly full width: {info['wideMain']}")
    if len(want_more) % 2 == 1 and info["wide"] != [want_more[-1]]:
        raise Fail(f"web page: lone last tile {want_more[-1]} not full width (wide: {info['wide']})")
    if len(want_more) % 2 == 0 and info["wide"]:
        raise Fail(f"web page: tiles wrongly full width: {info['wide']}")
    if info["graphW"] < info["tileW"] - 40 or info["graphW"] > info["tileW"]:
        raise Fail(f"web page: graph {info['graphW']} px wide in a Vario tile {info['tileW']} px wide")
    return info


def height_trace(v):
    """/api/height: the samples (height in metres, None for the gap between two flights)."""
    code, body = v._open("/api/height")
    n, step = struct.unpack("<HH", body[:4])
    raw = struct.unpack("<%dh" % n, body[4:4 + 2 * n])
    return [None if r == -32768 else r / 4.0 for r in raw], step


def t_height_trace(v, a):
    """The height trace: nothing is recorded on the ground, it is kept (not cleared) after landing, and a new flight
    continues it after a gap that marks the two flights as separate."""
    notes = []
    try:
        m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="fix", sats=10, hdop=0.9, speed=0, lat=51.5, lon=0.5)
        time.sleep(8)
        tr, _ = height_trace(v)
        if tr:
            raise Fail(f"{len(tr)} height samples recorded on the ground")
        notes.append("nothing recorded on the ground")
        take_off(v, climb_to=GROUND_M + 60, vz=4.0)
        time.sleep(14)
        tr, step = height_trace(v)
        if len(tr) < 5 or any(x is None for x in tr) or step != 2:
            raise Fail(f"flying: {len(tr)} samples every {step} s, gaps {sum(x is None for x in tr)}")
        n1 = len(tr)
        notes.append(f"{n1} samples in the first 14 s of flight")
        v.sim(speed=0, vz=-4.0, to=GROUND_M + 2)
        v.expect("APP", r"Landed after", m, timeout=90, what="landing")
        time.sleep(3)
        tr, _ = height_trace(v)
        n2 = len(tr)
        time.sleep(10)
        tr, _ = height_trace(v)
        if len(tr) != n2 or n2 < n1:
            raise Fail(f"after landing the trace has {n2} then {len(tr)} samples (before landing {n1}): it must neither grow nor be cleared")
        notes.append(f"after landing: kept ({n2} samples), no new ones in 10 s")
        take_off(v, climb_to=GROUND_M + 60, vz=4.0)
        time.sleep(8)
        tr, _ = height_trace(v)
        gaps = sum(x is None for x in tr)
        if gaps < 10 or tr[-1] is None or len(tr) <= n2 + gaps - 1:
            raise Fail(f"second flight: {len(tr)} samples, {gaps} gap markers (expected a gap of about 15 between the flights)")
        first_gap = next(i for i, x in enumerate(tr) if x is None)
        if first_gap < n2 - 2 or any(x is None for x in tr[:first_gap]) or any(x is None for x in tr[first_gap + gaps:]):
            raise Fail(f"the gap is not one block between the two flights: {[('-' if x is None else int(x)) for x in tr]}")
        notes.append(f"second flight continues after a gap of {gaps} markers")
        return "; ".join(notes)
    finally:
        v.sim(speed=0, vz=0, fly=0)
        v.action("reset_flight")


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
    bare = [g["g"] for g in groups if not g["l"] and g["g"] not in ("Advanced settings", "Quick settings")]
    if bare:
        raise Fail(f"web page: settings groups with no help link: {bare}")
    for g in groups:
        check_anchors([{"t": g["g"] + " / " + l["t"], "h": l["h"]} for l in g["l"]])
    shared = {g["g"]: [l["t"] for l in g["l"]] for g in groups}
    if shared["Units"] != ["Help", "More help"] or shared["Audio"] != ["Help"] or shared["Vario"] != ["Help"] \
            or shared["Engine RPM"] != ["Help"]:
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

    # With no GPS module the guide for a vario without one is offered; the microphone features stay.
    try:
        v.sim(hasGps=0)
        p = v.page()
        p.wait("document.body.classList.contains('nogps')&&!document.body.classList.contains('nomic')", 8,
               "page told there is no GPS (and a microphone)")
        p.tab("help")
        links = tab_links()
        if links[0]["t"] != "User guide" or "User_Guide_%28basic%29.md" not in links[0]["h"]:
            raise Fail(f"web page: first help link without a GPS is {links[0]}")
        if any("Basic" in l["t"] or "Advanced" in l["t"] for l in links):
            raise Fail(f"web page: the Help tab talks about versions: {[l['t'] for l in links]}")
        if any("adv-" in l["h"] for l in links) or not any("basic-spectrum" in l["h"] for l in links):
            raise Fail("web page: Help shortcuts without a GPS should be the common ones, including the Spectrum")
        p.tab("settings")
        adv_links = p.js("[...document.querySelectorAll('#settings summary')].filter(s=>s.firstChild.textContent!=='GPS')"
                         ".flatMap(s=>[...s.querySelectorAll('.help a')]).filter(a=>a.href.includes('#adv-')).length")
        if adv_links:
            raise Fail(f"web page: {adv_links} GPS help links on the settings page without a GPS")
        if p.js("[...document.querySelectorAll('#settings summary')].find(s=>s.firstChild.textContent==='Engine RPM')"
                ".querySelectorAll('.help a').length") != 1:
            raise Fail("web page: no help link on the Engine RPM group (the microphone is still fitted)")
        stub_docs(p)
        p.tab("help")
        p.js("[...document.querySelectorAll('#helpBody a.doc')][0].click()")
        p.wait("document.querySelector('#helpDoc h1')&&document.querySelector('#helpDoc h1').textContent.includes('Basic vario')", 6,
               "basic guide shown without a GPS")
        note += "; the guide for a vario without a GPS is offered then"
    finally:
        v.sim(hasGps=1)
    return note


def t_gps_detect(v, a):
    """The vario finds out for itself whether a GPS module is fitted, remembers one it has seen, and
    can be told by hand; the page follows without a reload."""
    v.set(gpsMode=0)  # Auto-detect
    try:
        reset(v, baro="on", alt=GROUND_M, vz=0, gps="silent")
        m = v.mark()
        v.sim(gpsForget=1)  # as a vario that has never seen a GPS, switched on just now
        v.expect("RDY", r"^Looking for GPS$", m, timeout=6, what="banner while it looks")
        st = v.status()
        if st["hw"]["gpsState"] != "unknown" or st["hw"]["gps"]:
            raise Fail(f"just after power-on the GPS state should be unknown: {st['hw']}")
        # nothing heard for the detection time (10 s): not fitted, and ready on the barometer alone
        _, _ = v.expect("GPS", r"GPS module: not fitted", m, timeout=20, what="GPS declared not fitted")
        v.expect("RDY", r"^ready$", m, timeout=15, what="ready without a GPS")
        st = v.status()
        if st["hw"]["gpsState"] != "absent" or "gps" in st or not st["ready"]:
            raise Fail(f"with no GPS heard: hw {st['hw']}, gps section {'gps' in st}, ready {st['ready']}")
        if "gpsMode" not in {f["k"] for f in v.get_json("/api/schema")} or "lzMode" in {f["k"] for f in v.get_json("/api/schema")}:
            raise Fail("settings without a GPS should list GPS module and nothing else of the GPS settings")
        p = v.page() if a.ui else None
        if p:
            p.wait("document.body.classList.contains('nogps')", 6, "page hides the GPS features")
            p.tab("settings")
            p.wait("!![...document.querySelectorAll('#settings summary')].find(s=>s.firstChild.textContent==='GPS')", 6,
                   "GPS group (module setting) listed")
            p.wait("![...document.querySelectorAll('#settings summary')].some(s=>s.firstChild.textContent==='Landing zone')", 6,
                   "no landing zone group without a GPS")

        # a GPS module starts sending: found within seconds, everything that needs it switches on
        m2 = v.mark()
        v.sim(gps="fix", sats=10, hdop=0.9, lat=51.5, lon=0.5, speed=0)
        v.expect("GPS", r"GPS module: present", m2, timeout=10, what="GPS found when it starts sending")
        st = v.status()
        if not st["hw"]["gps"] or st["hw"]["gpsState"] != "present" or "gps" not in st:
            raise Fail(f"after the GPS started sending: hw {st['hw']}, gps section {'gps' in st}")
        if p:
            p.wait("!![...document.querySelectorAll('#settings summary')].some(s=>s.firstChild.textContent==='Landing zone')", 10,
                   "settings list fetched again with the GPS groups")
            p.wait("!document.body.classList.contains('nogps')", 6, "page shows the GPS features")

        # once seen it is remembered: silence means "not responding", the GPS does not disappear
        m3 = v.mark()
        v.sim(gps="silent")
        v.expect("RDY", r"GPS not responding", m3, timeout=10, what="a GPS that went quiet shows as not responding")
        if v.status()["hw"]["gpsState"] != "present":
            raise Fail("a GPS that was seen and went quiet should still count as present")

        # by hand
        v.set(gpsMode=2)  # Not fitted
        end = time.time() + 6
        while v.status()["hw"]["gps"]:
            if time.time() > end:
                raise Fail("GPS module = Not fitted did not switch the GPS features off")
            time.sleep(0.5)
        if p:
            p.wait("document.body.classList.contains('nogps')", 8, "page follows Not fitted")
        v.set(gpsMode=1)  # Always present
        end = time.time() + 6
        while not v.status()["hw"]["gps"]:
            if time.time() > end:
                raise Fail("GPS module = Always present did not switch the GPS features on")
            time.sleep(0.5)
        if p:
            p.wait("!document.body.classList.contains('nogps')", 8, "page follows Always present")
        return "unknown -> not fitted after 10 s -> found when it starts sending -> kept when silent; set by hand both ways; page followed"
    finally:
        v.sim(gps="silent")
        v.set(gpsMode=TEST_SETTINGS["gpsMode"])


def t_ui_flight(v, a):
    """Flight tab: banner and every tile show the simulated situation, and follow changes."""
    m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="fix", sats=10, hdop=0.9,
              speed=0, track=90, lat=51.5, lon=0.5, batt=4.00, noise=-60)
    v.expect("RDY", r"^ready$", m, timeout=25)
    st = v.status()
    p = v.page()
    p.tab("flight")
    p.wait_text("t-gps", r"^strong$", 8)
    p.wait_text("ready-t", r"^Ready to fly$", 8)
    if p.js("document.getElementById('ready').className") != "ready ok":
        raise Fail("web page: ready banner is not green")
    p.wait_text("ready-s", r"^10 / 6 satellites$")
    agl = p.wait_text("t-agl", r"^-?[0-7] ft$", 20, "Height AGL tile about 0 ft on the ground")
    near(p.wait_text("t-msl", r"^\d+ ft MSL$").split()[0], 328, 8, "MSL under the height tile (ft)")
    p.wait_text("t-vario", r"^[+-]0\.[01] m/s$")
    p.wait_text("t-speedv", r"^0 km/h$")
    p.wait_text("t-track", r"^track \d+°$")
    p.wait_text("t-flight", r"^Grounded$")
    near(p.wait_text("t-lz", r"^\d+ m$", what="distance to the landing zone").split()[0], 0, 6, "LZ distance")
    p.wait_text("t-lzs", r"ft MSL$")
    p.wait_text("t-wind", r"^unknown$")
    p.wait_text("t-gpss", r"^10 satellites · fix$")
    p.wait_text("t-batts", r"^4\.00 V · \d+%$")
    if not st["batt"]["learned"]:
        p.wait_text("t-batts", r"^4\.00 V · 80%$")
    p.wait_text("t-noise", r"^-60 dBFS$")
    p.wait_text("t-noises", rf"^vario vol {a.volume}%$")
    p.wait_text("t-rpm", r"^(off|\d+ rpm)$")
    p.wait_text("hver", rf"^v{re.escape(st['version'])} debug$")
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
    p.wait_text("t-speedv", r"^43 km/h$", 8, "12 m/s shown in km/h")
    v.sim(speed=0, vz=1.5)  # not both at once: speed plus climb would be a takeoff
    p.wait_text("t-vario", r"^\+1\.[3-6] m/s$", 20)
    v.sim(vz=0, batt=3.30)
    p.wait_text("t-batts", r"^3\.30 V · 0%$")
    v.sim(gps="silent")
    p.wait_text("t-gps", r"^no data$")
    p.wait_text("ready-t", r"^GPS not responding$")
    p.wait_text("t-speedv", r"^–$", 8, "no speed without a fix")
    if p.js("document.getElementById('ready').className") != "ready wait":
        raise Fail("web page: banner should be amber while not ready")
    v.sim(gps="fix", sats=4, hdop=4.2)
    p.wait_text("t-gps", r"^good$", 8)
    p.wait_text("t-gpss", r"^4 satellites · fix$")
    p.wait_text("ready-t", r"^Waiting for satellites$")
    p.wait_text("ready-s", r"^4 / 6 satellites$")
    p.wait_text("t-speedv", r"^0 km/h$", 8, "weak-fix speed shown as 0")
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
    p.click("#b-mute-g")
    v.expect("SAY", r"\[urgent\] vario_muted$", m, timeout=4, what="Mute button")
    p.wait_text("b-mute-g", r"^Unmute vario$")
    p.wait_text("t-mute", r"^muted$")
    p.wait_text("toast", r"^Done$")
    m2 = v.mark()
    p.click("#b-mute-g")
    v.expect("SAY", r"\[urgent\] vario_on$", m2, timeout=4, what="Unmute button")
    p.wait_text("b-mute-g", r"^Mute vario$")
    p.click('button[data-a="test_voice"]')
    v.expect("SAY", r"\[urgent\] system_ready ", m2, timeout=4, what="Test voice button")
    time.sleep(4)
    p.tab("settings")
    p.js("document.querySelectorAll('#settings details').forEach(d=>d.open=true)")
    p.wait("document.getElementById('b-lz-here')!==null", 5, "Use current position button in the Landing zone settings")
    if p.js("document.querySelector('button[data-a=\"lz_here\"], button[data-a=\"lz_takeoff\"]')"):
        raise Fail("web page: Set LZ manually / LZ = takeoff are still on the page as buttons")
    p.click('#b-lz-here')
    p.wait_text("toast", r"^No GPS fix$", 5, "Use current position refused without a fix")
    p.tab("flight")

    v.sim(gps="fix", sats=10, hdop=0.9, speed=0, lat=51.5, lon=0.5)
    v.expect("RDY", r"^ready$", m2, timeout=25)
    m3 = v.mark()
    p.tab("settings")
    p.click('#b-lz-here')
    v.expect("SAY", r"\] lz_set$", m3, timeout=4, what="Use current position button")
    s = v.settings()
    near(s["lzMode"], 1, 0, "LZ mode after Use current position (manual)")
    near(s["lzLat"], 51.5, 0.001, "saved LZ latitude")
    p.wait("tInput('lzMode') && tInput('lzMode').value==='1'", 12, "settings tab shows the manual LZ")
    p.wait("Math.abs(+tInput('lzLat').value-51.5)<0.001", 12, "settings tab shows the saved latitude")
    # back to the takeoff point is the Landing zone setting itself
    p.js("tSet('lzMode', 0)")
    p.wait_text("saveState", r"^Saved", 5)
    near(v.settings()["lzMode"], 0, 0, "LZ mode set back to takeoff point in Settings")
    return "mute, test voice, Use current position (refused without a fix, accepted with one), LZ mode back to takeoff in Settings"


def t_ui_pilot(v, a):
    """The flight tab follows the phase and puts the pilot first: on the ground the setup card (Test voice, Set LZ here,
    LZ = takeoff) is shown and in flight it is gone, leaving Landing approach, Emergency glide, Tell me and Mute; the
    alert strip shows a low or critical battery (with minutes and landing zone distance), a failed barometer and a lost
    GPS in flight and clears when they recover; the GPS tile says good / weak / none, the Battery tile minutes left; the
    Background noise tile is hidden unless asked for; Quick settings come first on the Settings tab and follow their
    groups; Tell me and Reset flight state buttons work."""
    notes = []
    try:
        m = reset(v, baro="on", alt=GROUND_M, vz=0, baroOffset=150, gps="fix", sats=10, hdop=0.9, speed=0, track=90,
                  lat=51.5, lon=0.5, batt=4.0)
        v.expect("RDY", r"^ready$", m, timeout=25)
        p = v.page()
        p.tab("flight")
        p.wait("document.getElementById('groundActions').offsetParent!==null", 8, "the ground Actions card shown on the ground")
        if p.js("document.getElementById('flightActions').offsetParent!==null"):
            raise Fail("web page: the in-flight Actions card is shown on the ground")
        heads = p.js("[...document.querySelectorAll('#groundActions h2, #flightActions h2')].map(h=>h.textContent)")
        if heads != ["Actions", "Actions"]:
            raise Fail(f"web page: the action cards are headed {heads}, expected Actions on both")
        ground = p.js("[...document.querySelectorAll('#groundActions button')].map(b=>b.textContent)")
        if sorted(ground) != ["Audio test", "Mute vario"]:
            raise Fail(f"web page: the ground Actions card holds {ground}")
        if p.js("document.getElementById('alerts').children.length") != 0:
            raise Fail("web page: an alert shown on the ground with a good battery")
        p.wait_text("t-gps", r"^strong$", 8, "GPS tile: signal quality as the value")
        p.wait_text("t-gpss", r"^10 satellites · fix$")
        if p.js("document.getElementById('tile-noise').offsetParent!==null"):
            raise Fail("web page: the Background noise tile is shown without being asked for")
        notes.append("ground: setup card shown, no alerts, GPS tile says strong")

        # in flight
        take_off(v, climb_to=GROUND_M + 60, vz=4.0)
        p.wait("document.body.classList.contains('flying')", 20, "the page knows it is flying")
        if p.js("document.getElementById('groundActions').offsetParent!==null"):
            raise Fail("web page: the ground Actions card is still shown in flight")
        flight = p.js("[...document.querySelectorAll('#flightActions button')].map(b=>b.textContent)")
        if flight != ["Landing support", "Emergency support", "Update me", "Mute vario"]:
            raise Fail(f"web page: the in-flight Actions card holds {flight}")
        if p.js("document.getElementById('flightActions').offsetParent===null"):
            raise Fail("web page: the in-flight Actions card is not shown in flight")
        m = v.mark()
        p.click("#b-tell")
        v.expect("SAY", r"^\[high\] .*landing_zone", m, timeout=5, what="the Update me button")
        v.expect("SAY", r"^\[high\] .*battery", m, timeout=5, what="the Update me button: battery")
        notes.append("flight: the ground card is gone, the in-flight card holds Landing support, Emergency support, Update me and Mute; Update me speaks")

        def strip():
            return p.js("[...document.querySelectorAll('#alerts .alert')].map(e=>e.className+'|'+e.textContent)")

        v.sim(batt=3.40)
        p.wait("document.querySelectorAll('#alerts .alert.warn').length===1", 8, "low battery alert")
        s = strip()[0]
        if "Battery low" not in s or "LZ" not in s:
            raise Fail(f"web page: low battery alert reads {s!r}")
        v.sim(batt=3.20)
        p.wait("document.querySelectorAll('#alerts .alert.bad').length===1", 8, "critical battery alert")
        s = strip()[0]
        if "Battery critical" not in s or "switch me off" not in s:
            raise Fail(f"web page: critical battery alert reads {s!r}")
        v.sim(batt=3.9)
        p.wait("document.querySelectorAll('#alerts .alert').length===0", 8, "alert cleared with the battery")
        v.sim(baroFault="dead")
        p.wait("[...document.querySelectorAll('#alerts .alert.bad')].some(e=>/Barometer failed/.test(e.textContent))", 12, "barometer alert")
        v.sim(baroFault="none")
        p.wait("document.querySelectorAll('#alerts .alert').length===0", 20, "barometer alert cleared")
        v.sim(gps="silent")
        p.wait("[...document.querySelectorAll('#alerts .alert.warn')].some(e=>/No GPS/.test(e.textContent))", 10, "no GPS alert")
        v.sim(gps="fix")
        p.wait("document.querySelectorAll('#alerts .alert').length===0", 15, "no GPS alert cleared")
        notes.append("alert strip: low and critical battery, barometer, no GPS; each clears")

        # battery tile: minutes left
        txt = p.text("t-batt")
        if not re.search(r"^(~.+m|\d+%)$", txt):
            raise Fail(f"web page: battery tile reads {txt!r}")
        p.wait_text("t-batts", r"^\d\.\d\d V · \d+%$", 6)
        notes.append(f"battery tile {txt!r}")

        # the Background noise tile on request
        p.tab("settings")
        p.js("tSet('showEngTiles', true)")  # the page's own copy of the settings follows its own changes
        p.tab("flight")
        p.wait("document.getElementById('tile-noise').offsetParent!==null", 12, "noise tile shown once asked for")
        p.tab("settings")
        p.js("tSet('showEngTiles', false)")
        p.tab("flight")
        p.wait("document.getElementById('tile-noise').offsetParent===null", 12, "noise tile hidden again")
        notes.append("noise tile only on request")

        # quick settings first, and in step with the group copy
        p.tab("settings")
        p.wait("document.querySelector('#settings > details') && document.querySelector('#settings > details').id==='quickSettings'", 10,
               "Quick settings is the first card")
        keys = p.js("[...document.querySelectorAll('#quickSettings [data-key]')].map(i=>i.dataset.key)")
        want = {"varioVolume", "speechVolume", "heightUnit", "distUnit", "speedUnit", "circuitSide", "glideRatio", "trimAirspeedMs"}
        if set(keys) != want:
            raise Fail(f"web page: Quick settings holds {sorted(keys)}, expected {sorted(want)}")
        p.js("(()=>{const i=document.querySelector('#quickSettings [data-key=glideRatio]');i.value='7.5';i.dispatchEvent(new Event('change'));return 1})()")
        p.wait("[...document.querySelectorAll('[data-key=glideRatio]')].every(i=>i.value==='7.5')", 6, "both copies of glide ratio show 7.5")
        end = time.time() + 6
        while abs(v.settings()["glideRatio"] - 7.5) > 0.01:
            if time.time() > end:
                raise Fail("web page: the glide ratio set in Quick settings was not saved")
            time.sleep(0.5)
        notes.append("Quick settings first, same settings as their groups, saved")

        # reset the flight state from the Logs & system tab
        p.tab("system")
        m = v.mark()
        p.click('button[data-a="reset_flight"]')
        v.expect("APP", r"Flight state reset by the pilot", m, timeout=6, what="Reset flight state button")
        v.wait_nav(lambda n: n["fly"] == "0", 6, "not flying after the reset")
        p.tab("flight")
        p.wait("!document.body.classList.contains('flying')", 10, "the page shows the ground again")
        p.wait("document.getElementById('groundActions').offsetParent!==null", 10, "the ground Actions card is back")
        notes.append("Reset flight state button ends the flight and brings the ground Actions card back")
    finally:
        v.sim(gps="fix", baroFault="none", batt="off")
        v.set(showEngTiles=False, glideRatio=TEST_SETTINGS.get("glideRatio", 5.0))
    return "; ".join(notes)


def t_ui_layout(v, a):
    """What a look at the screen would show, on a 360 px wide phone (the user's) and a 320 px one: no text cut off ("39 k..."),
    nothing drawn over anything else, nothing off the edge, in the ground view, flying, at 120 km/h, with other units, a far
    landing zone, three alerts at once, the Settings and Logs tabs. Saves a screenshot of each to results/ui/ to look at."""
    notes = []
    p = None
    try:
        v.set(windManual=True, windFromDeg=250, windSpeedMs=4.0)
        on_ground_with_fix(v)
        p = v.page()
        p.tab("flight")
        p.wait("document.getElementById('t-lzv').textContent.length>0", 8, "landing zone tile filled")
        p.inspect("ground view")
        p.screenshot("1_flight_ground")
        notes.append("ground view")

        airborne(v, hdg=340, air=10, wfrom=250, wspd=4, climb=60.0)
        p.wait("document.querySelector('#tile-speed .cmp').offsetParent!==null", 15, "the compass shown while moving")
        p.inspect("flying")
        p.screenshot("2_flight_moving")
        notes.append("flying")

        v.sim(air=30)
        p.wait(r"/^1\d\d /.test(document.getElementById('t-speedv').textContent)", 15, "a three-digit speed")
        p.inspect("at 120 km/h")
        p.screenshot("3_flight_fast")
        notes.append("120 km/h")
        v.sim(air=10)

        # far landing zone and other units: the longest texts
        v.sim(lat=52.0)
        p.wait("document.getElementById('t-lzv').textContent.indexOf('km')>0", 15, "a distance in km")
        p.inspect("landing zone 55 km away")
        p.screenshot("4_flight_far")
        for name, sets in (("mph and miles", {"speedUnit": 1, "distUnit": 1, "heightUnit": 0}), ("knots and metres", {"speedUnit": 2, "distUnit": 0, "heightUnit": 1})):
            p.tab("settings")
            for k, val in sets.items():
                p.js(f"tSet('{k}', {val})")
            p.tab("flight")
            time.sleep(2.5)
            p.inspect(name)
            p.screenshot("5_flight_" + name.split()[0])
            notes.append(name)
        p.tab("settings")
        for k, val in {"speedUnit": 0, "distUnit": 0, "heightUnit": 0}.items():
            p.js(f"tSet('{k}', {val})")
        p.tab("flight")
        v.sim(lat=51.5)

        # three alerts at once
        v.sim(batt=3.20, baroFault="dead", gps="silent")
        p.wait("document.querySelectorAll('#alerts .alert').length>=3", 25, "three alerts in the strip")
        p.inspect("three alerts")
        p.screenshot("6_flight_alerts")
        notes.append("three alerts")
        v.sim(batt="off", baroFault="none", gps="fix")
        p.wait("document.querySelectorAll('#alerts .alert').length===0", 25, "alerts cleared")

        p.phone(320, 640)  # an older, narrower phone
        time.sleep(1)
        p.inspect("320 px wide, flying")
        p.screenshot("7_flight_320px")
        notes.append("320 px wide")
        p.phone()

        p.tab("settings")
        time.sleep(1)
        p.inspect("settings tab")
        p.screenshot("8_settings")
        p.tab("system")
        time.sleep(1)
        p.inspect("logs and system tab")
        p.screenshot("9_system")
        notes.append("settings and logs tabs")
        v.action("reset_flight")
        v.sim(speed=0)
    finally:
        try:
            if p:
                p.phone()
        except Fail:
            pass
        v.set(windManual=TEST_SETTINGS["windManual"], speedUnit=0, distUnit=0, heightUnit=TEST_SETTINGS.get("heightUnit", 0))
        v.sim(lat=51.5, batt="off", baroFault="none", gps="fix")
    return "no text cut off, nothing overlapping or off screen in: " + ", ".join(notes) + f" (screenshots in {SHOT_DIR})"


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
    p.wait("(()=>{const w=document.querySelector('#tile-wind .sockw'),a=document.getElementById('a-wind');"
           "return w.offsetParent!==null&&/rotate/.test(a.style.transform)})()", 8,
           "wind from the takeoff run shown as a windsock once moving")
    p.wait("getComputedStyle(document.getElementById('a-lz')).display!=='none'", 8, "landing zone arrow shown once moving")
    p.wait("(()=>{const c=document.querySelector('#tile-speed .cmp'),a=document.getElementById('a-track');"
           "return c.offsetParent!==null&&/rotate/.test(a.style.transform)})()", 8,
           "ground speed tile compass shown once moving, with the arrow turned to the track")
    letters = p.js("[...document.querySelectorAll('#tile-speed .cmp text')].map(t=>t.textContent).join('')")
    if letters != "NESW":
        raise Fail(f"web page: the compass letters read {letters!r}, expected NESW (N at the top)")
    p.wait("!/bearing/.test(document.getElementById('t-lzs').textContent)", 4, "no bearing on the landing zone tile")
    p.wait_text("t-winds", r"^from \d+° · takeoff$")
    p.wait_text("t-wind", r"^\d+ km/h$", 8, "the wind speed is the tile's main value while moving, with the direction under the windsock")
    check_height_graph(p)  # on the ground nothing is recorded: the graph draws once the flight has been recorded for a few seconds
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
    if not re.search(r"^\d+ kB free$", p.js("document.querySelector('#logs p').textContent")):
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
        adv_order = ["Battery", "Wi-Fi", "Firmware Update", "GPS", "Engine RPM", "Wind", "Logging", "Glide"]
        main_groups = [g for g in groups if g not in adv_order]
        adv_groups = [g for g in adv_order if g in groups]
        shown = p.js("[...document.querySelectorAll('#settings>details>summary')].map(s=>s.firstChild.textContent)")
        want = ["Quick settings"] + main_groups + (["Advanced settings"] if adv_groups else [])  # Quick settings: the pilot's card, first
        if shown != want:
            raise Fail(f"web page: top-level setting groups {shown}, expected {want}")
        shown_adv = p.js("[...document.querySelectorAll('#advSettings>details>summary')].map(s=>s.firstChild.textContent)")
        if shown_adv != adv_groups:
            raise Fail(f"web page: Advanced settings holds {shown_adv}, expected {adv_groups}")
        if p.js("document.getElementById('advSettings').open"):
            raise Fail("web page: Advanced settings should start closed")
        rows = p.js("document.querySelectorAll('#settings details:not(#quickSettings) label.row').length")  # (Quick settings repeats 8 of them)
        near(rows, len(schema), 0, "number of settings shown")
        missing = p.js("schema.filter(f=>!tInput(f.k)).map(f=>f.k)")
        if missing:
            raise Fail(f"web page: no input found for {missing}")
        p.wait_text("w-ap", r"^192\.168\.4\.1 · \d+ connected$", 4)  # the status line has no name: the name is the setting under it
        # The vario's own names are plain settings: the boxes show the stored values, which are what the phone sees
        for k in ("bleName", "wifiSsid"):
            want = v.settings()[k]
            p.wait(f"tInput('{k}').value===" + json.dumps(want), 6, f"{k} box shows the stored value")
        # The firmware update box is in Advanced settings / Firmware Update (not on the Logs & system tab), version shown, guarded
        where = p.js("(e=>e?[...e.closest('details').querySelector('summary').childNodes].filter(n=>n.nodeType===3).map(n=>n.textContent).join(''):null)(document.getElementById('fwcard'))")
        if where != "Firmware Update" or not p.js("!!document.getElementById('fwcard').closest('#advSettings')"):
            raise Fail(f"web page: the firmware update box is in {where!r}, expected Advanced settings / Firmware Update")
        st0 = v.status()
        p.wait_text("fwver", rf"^Installed: {re.escape(st0['version'])} · git {re.escape(st0['git'])} \(debug build\)$", 5)
        p.click("#fwgo")
        p.wait_text("toast", r"^Choose a \.bin file first$", 3, "Install refused without a file")
        if p.js("document.getElementById('fwpass').type") != "password":
            raise Fail("web page: update password box is not masked")
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
        # A Restart button appears at the bottom of Settings only while a change that needs a restart is waiting
        if p.js("document.getElementById('restartBar').offsetParent!==null"):
            raise Fail("web page: the Restart button is shown with no restart needed")
        was = p.js("tInput('gpsLed').checked")
        p.js("tSet('gpsLed', " + ("false" if was else "true") + ")")
        p.wait_text("saveState", r"^Saved", 5)
        p.wait("document.getElementById('restartBar').offsetParent!==null", 6, "Restart button shown after a setting that needs a restart")
        p.js("tSet('gpsLed', " + ("true" if was else "false") + ")")
        p.wait_text("saveState", r"^Saved", 5)
        p.click('#restartBar button')
        time.sleep(4)
        p.wait("document.getElementById('restartBar').offsetParent===null && document.getElementById('t-agl')!==null", 90,
               "Restart button gone after the restart")
        if p.js("tInput('wifiPass').type") != "password" or p.js("tInput('serverPass').type") != "password":
            raise Fail("web page: Wi-Fi passwords are not masked")
        # The maintenance connection settings (for the maker, not pilots) are in a sub-section that shows only while its switch is on.
        was = bool(v.settings()["maintWifi"])
        hid = lambda: p.js("document.querySelector('.maintbox').hidden")
        vis_now = lambda: p.js("(()=>{const b=document.querySelector('.maintbox');return b.offsetParent!==null&&b.querySelector('[data-key=serverSsid]').offsetParent!==null})()")
        for want in (not was, was):
            p.js("tInput('maintWifi').click()")
            p.wait("(()=>{const b=document.querySelector('.maintbox');return (b.offsetParent!==null)===" + ("true" if want else "false") + "})()", 4,
                   "maintenance settings " + ("shown" if want else "hidden") + " by their switch")
            p.wait_text("saveState", r"^Saved", 5)
        if v.settings()["maintWifi"] != was:
            raise Fail("web page: the maintenance switch did not go back")
        txt = p.js("document.getElementById('settings').textContent")
        for s in ("Enable maintenance server connection", "WiFi SSID", "WiFi password", "Stop looking after", "Assigned IP address"):
            if s not in txt:
                raise Fail(f"web page: Wi-Fi settings lack the label '{s}'")
        if re.search(r"Server (network|IP)", txt, re.I):
            raise Fail("web page: an old 'Server network' / 'Server IP' label is still on the settings tab")
        p.js("document.querySelectorAll('#settings details').forEach(d=>d.open=true)")
        p.check_fits("settings tab with every group open")
    finally:
        v.set(climbThreshold=0.3, takeoffGainM=5.0, heightUnit=0, boundaryClimbing=True,
              boundariesUp="15.24,30.48", pilotName="")
    return f"{len(groups)} groups, {len(schema)} settings; number, feet, list, checkbox gate, unit switch, text"


def t_spectrum_setting(v, a):
    """Advanced setting "Show the Spectrum tab": off by default, the tab is removed from the tab list when
    off and back when on."""
    if not v.status()["hw"].get("spectrum", True) and "spectrumEnabled" not in v.settings():
        return "skipped: firmware built without the spectrum feature"
    p = v.page()
    try:
        v.set(spectrumEnabled=False)
        p.wait("document.querySelector('button[data-tab=spectrum]').offsetParent===null", 8, "Spectrum tab removed")
        if v.status()["hw"]["spectrum"]:
            raise Fail("status still says the spectrum is available with the setting off")
        v.set(spectrumEnabled=True)
        p.wait("document.querySelector('button[data-tab=spectrum]').offsetParent!==null", 8, "Spectrum tab back")
        return "tab removed when off, back when on (default is off: " + str(settings_default_off()) + ")"
    finally:
        v.set(spectrumEnabled=TEST_SETTINGS["spectrumEnabled"])


def settings_default_off():
    src = open(os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "src", "Settings.h"), encoding="utf-8").read()
    m = re.search(r"bool spectrumEnabled = (\w+);", src)
    if not m or m.group(1) != "false":
        raise Fail("spectrumEnabled should default to false in Settings.h")
    return True


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
    order = p.js("[...document.querySelectorAll('nav.tabs button[data-tab]')].map(b=>b.dataset.tab)")
    if order[:3] != ["flight", "system", "settings"] or order[-1] != "help":
        raise Fail(f"web page: tab order {order}, expected Flight, Logs & system, Settings, (Spectrum), Help")
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
    if p.js("document.querySelector('#systemPage #fwgo, #systemPage #fwfile')"):
        raise Fail("web page: the firmware update box is still on the Logs & system tab")
    p.check_fits("logs & system tab")
    return "5 tabs switch, debug log live/pause/clear, version and system line"


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
    ("vario_sink", t_vario_sink), ("vario_thresholds", t_vario_thresholds), ("vario_logic", t_vario_logic),
    ("mute", t_mute), ("auto_volume", t_auto_volume), ("rpm_logic", t_rpm_logic), ("rpm", t_rpm), ("rpm_speech", t_rpm_speech), ("rpm_glitch", t_rpm_glitch), ("geo_logic", t_geo_logic), ("wind_logic", t_wind_logic),
    ("circuit_logic", t_circuit_logic), ("battery_logic", t_battery_logic), ("flight_logic", t_flight_logic), ("button_logic", t_button_logic), ("factory_reset", t_factory_reset), ("height_logic", t_height_logic), ("height_trace", t_height_trace), ("maint_voice", t_maint_voice), ("names_default", t_names_default), ("status_logic", t_status_logic), ("alerts_logic", t_alerts_logic), ("resume_logic", t_resume_logic), ("audio_logic", t_audio_logic), ("rpmtrack_logic", t_rpmtrack_logic), ("baro_logic", t_baro_logic), ("wind", t_wind), ("wind_faults", t_wind_faults), ("wind_change", t_wind_change), ("circuit_right", t_circuit_right), ("circuit_slow_pilot", t_circuit_slow_pilot), ("circuit_left_wind", t_circuit_left_wind),
    ("circuit_events", t_circuit_events),
    ("fault_baro", t_fault_baro), ("fault_gps", t_fault_gps), ("fault_battery", t_fault_battery), ("landing_gps_quality", t_landing_gps_quality), ("landing_hover", t_landing_hover), ("button_actions", t_button_actions), ("alerts_flight", t_alerts_flight), ("resume_flight", t_resume_flight), ("battery_flight", t_battery_flight),
    ("memory_events", t_memory_events), ("soak", t_soak),
    ("gps_states", t_gps_states), ("flight", t_flight), ("baro_only", t_baro_only),
    ("guidance", t_guidance), ("battery", t_battery), ("battery_learner", t_battery_learner),
    ("speech", t_speech), ("volume_ramp", t_volume_ramp), ("glide", t_glide),
    ("variant_no_gps", t_variant_no_gps), ("variant_no_mic", t_variant_no_mic), ("gps_detect", t_gps_detect),
    ("ui_flight", t_ui_flight), ("ui_pilot", t_ui_pilot), ("ui_layout", t_ui_layout), ("ui_actions", t_ui_actions), ("ui_guidance", t_ui_guidance),
    ("ui_settings", t_ui_settings), ("ui_spectrum", t_ui_spectrum), ("spectrum_setting", t_spectrum_setting), ("ui_system", t_ui_system), ("ui_help", t_ui_help),
    ("ota_guard", t_ota_guard), ("memory", t_memory),
]


# Tests during which the log legitimately loses lines (the vario restarts, so its log ring starts again).
# Anywhere else a dropped line means a test may have judged an incomplete log, and the test is failed.
# memory_events floods the vario with requests and resets on purpose, faster than the log feed can follow.
# The *_logic tests run production code over thousands of simulated seconds in one request (the circuit suite logs
# every phase change and trace line, as it would in flight) and assert on the suite's own report, never on the log.
DROPS_ALLOWED = {"boot", "memory_events", "ui_layout"}  # ui_layout judges what is on the screen, and taking screenshots slows the log feed


def isolate(v, a):
    """Every test starts from the same place: simulation off, flight logic reset, test settings. A failed
    test cannot leave anything behind for the next one to trip over (or, worse, to pass because of)."""
    v.sim(off=1)
    v.sim(reset=1)
    v.set(**TEST_SETTINGS, speechVolume=a.volume, varioVolume=a.volume)


def outside(v):
    """Anybody but the harness using the vario? The vario counts web clients that made a request in the last 15 s
    without the harness's X-HIL header (whichever network they are on, so a phone page counts too), the stations
    joined to its own access point, and BLE connections. (The first two keys exist from the build of this change on.)"""
    try:
        st = v.status()
    except Fail:
        return None
    w = st.get("wifi", {})
    return dict(web=w.get("webOthers"), sta=w.get("apClients"), ble=st.get("ble"))


def outside_text(o):
    if not o:
        return "unknown"
    return f"web pages {o['web'] if o['web'] is not None else '?'}, Wi-Fi stations {o['sta']}, BLE {o['ble']}"


def has_outside(o):
    return bool(o) and any((o.get(k) or 0) > 0 for k in ("web", "sta", "ble"))


def diagnostics(v, since):
    """What to look at when a test failed: the vario's state and the last of its log."""
    out = []
    try:
        st = v.status()
        w = st.get("wind", {})
        out.append(f"state: uptime {st['uptime']} s, flying {st['flying']}, agl {st['agl']:.0f} m, vario {st['vario']:+.2f}, "
                   f"baro ok {st['baro']['ok']}, fix {st.get('gps', {}).get('fix')}, wind {w.get('src')} {w.get('from', 0):.0f}/{w.get('speed', 0):.1f}, "
                   f"heap {st['heap']} (lowest {st['heapMin']})")
    except Exception as e:  # noqa: BLE001 - diagnostics must never hide the real failure
        out.append(f"state: could not read the status ({e})")
    try:
        out.append("sim: " + json.dumps(v.get_json("/api/sim")))
    except Exception as e:  # noqa: BLE001
        out.append(f"sim: could not read ({e})")
    try:
        v.poll()
        tail = [repr(l) for l in v.lines[since:] if l.tag not in ("BARO", "MEM")][-18:]
        out.append("last log lines:\n          " + "\n          ".join(tail) if tail else "no log lines during the test")
    except Exception as e:  # noqa: BLE001
        out.append(f"log: could not read ({e})")
    return "\n        ".join(out)


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("names", nargs="*", help="run only tests whose name contains one of these")
    ap.add_argument("--host", default="paravario.local")
    ap.add_argument("--no-audio", dest="audio", action="store_false", help="skip microphone checks")
    ap.add_argument("--no-ui", dest="ui", action="store_false", help="skip the web page (browser) tests")
    ap.add_argument("--volume", type=int, default=20, help="speech and vario volume during the tests (%%)")
    ap.add_argument("--memory", action="store_true", help="print the vario's free memory after each test")
    ap.add_argument("--soak-minutes", type=int, default=10, help="length of the soak test's flight (default 10)")
    ap.add_argument("--list", action="store_true")
    ap.add_argument("--shuffle", nargs="?", const=-1, type=int, metavar="SEED", help="run the selected tests in a random order "
                    "(to show that they do not depend on each other); the seed is printed so a failing order can be repeated")
    ap.add_argument("-v", "--verbose", action="store_true", help="print every log line received")
    a = ap.parse_args()
    tests = [(n, f) for n, f in TESTS if (not a.names or any(w in n for w in a.names))
             and (a.ui or not n.startswith("ui_"))]
    unmatched = [w for w in a.names if not any(w in n for n, _ in TESTS)]
    if unmatched or not tests:
        known = ", ".join(n for n, _ in TESTS)
        why = (f"no test matches {', '.join(repr(w) for w in unmatched)}" if unmatched
               else "no tests selected (--no-ui removed every test you named)")
        print(f"ERROR: {why}.\nTests: {known}", file=sys.stderr)
        return 2
    if a.shuffle is not None:
        seed = a.shuffle if a.shuffle >= 0 else int(time.time()) % 100000
        random.Random(seed).shuffle(tests)
        print(f"Test order shuffled, seed {seed}: {' '.join(n for n, _ in tests)}")
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
    touched = []  # tests during which something other than the harness was using the vario
    missing_clips = set()  # voice clips the vario wanted and did not have (the filesystem needs uploading)
    cleanup = []  # (step, error or None)
    try:
        for name, fn in tests:
            t0 = time.time()
            try:
                v.poll()  # lines dropped while the previous test ended belong to that test, not this one
            except Fail:
                pass
            dropped0 = v.dropped
            try:
                isolate(v, a)
            except Fail as e:
                results.append((name, False, f"could not start from a clean state: {e}"))
                print(f"  FAIL  {name:16} could not start from a clean state: {e}")
                continue
            since = len(v.lines)
            ext0 = outside(v)
            try:
                note = fn(v, a)
                v.poll()
                lost = v.dropped - dropped0
                if lost and name not in DROPS_ALLOWED and not name.endswith("_logic"):
                    raise Fail(f"{lost} log line(s) were dropped during the test, so it may have judged an incomplete log "
                               f"(the test itself passed: {note})")
                results.append((name, True, note))
                print(f"  PASS  {name:16} {time.time() - t0:5.1f} s  {note}")
            except Fail as e:
                results.append((name, False, str(e)))
                print(f"  FAIL  {name:16} {time.time() - t0:5.1f} s  {e}\n        {diagnostics(v, since)}")
            for l in v.lines[since:]:
                mc = re.search(r"Missing voice clip /v/(\w+)\.adp", l.msg) if l.tag == "AUD" else None
                if mc:
                    missing_clips.add(mc.group(1))
            ext1 = outside(v)
            if has_outside(ext0) or has_outside(ext1):
                touched.append(name)
                print(f"        WARNING: somebody else was using the vario during this test (before: {outside_text(ext0)}; "
                      f"after: {outside_text(ext1)}): its timing, memory and log results may be affected")
            if a.memory:
                try:
                    st = v.status()
                    print(f"        memory: free {st['heap']} lowest {st['heapMin']} largest block {st['heapBlock']}"
                          f" | others: {outside_text(ext1)}")
                except Fail:
                    pass
    finally:
        if a.memory:  # what the vario was doing each time free memory reached a new low
            for l in v.lines:
                mm = re.search(r"-> (\d+) B", l.msg) if l.tag == "MEM" else None
                if mm and int(mm.group(1)) < 24000:
                    print(f"        low memory: {l.msg}")
        v.stop = True

        # Cleanup: each step on its own, so one failing does not stop the others, and each is reported.
        def step(label, fn):
            try:
                fn()
                cleanup.append((label, None))
            except Exception as e:  # noqa: BLE001
                cleanup.append((label, str(e)[:120]))

        reloads = getattr(v.browser, "reloads", 0) if v.browser else 0
        if v.browser:
            step("close the browser", v.browser.close)
        step("simulation off", lambda: v.sim(off=1))
        step("flight logic reset", lambda: v.sim(reset=1))
        step("settings restored", lambda: v.set(**saved))

        def delete_stray():
            stray = [f["n"] for f in v.get_json("/api/logs")["files"] if f["n"] not in logs_before]
            for fname in stray:  # IGC files a failed test left behind
                v._open("/api/log?name=" + urllib.parse.quote(fname), method="DELETE")
            cleanup.append((f"{len(stray)} leftover test log(s) deleted", None)) if stray else None

        step("leftover test logs", delete_stray)
        if reloads:
            print(f"Note: the web page came up without its settings {reloads} time(s) and was reloaded.")
        bad = [(l, e) for l, e in cleanup if e]
        print("Cleanup: " + ", ".join(l for l, e in cleanup if not e) + ("." if not bad else ";"))
        for l, e in bad:
            print(f"WARNING: cleanup step '{l}' failed: {e}")
    failed = [r for r in results if not r[1]]
    if missing_clips:
        print(f"\nWARNING: the vario had no voice clip for: {', '.join(sorted(missing_clips))} (upload the filesystem: pio run -e release -t uploadfs)")
    if touched:
        print(f"\nNOTE: {len(touched)} test(s) ran while a phone, browser or BLE client was connected: {', '.join(touched)}")
    print(f"\n{len(results) - len(failed)} passed, {len(failed)} failed"
          f"{', ' + str(v.dropped) + ' log lines missed' if v.dropped else ''}")
    if failed:
        return 1
    return 3 if any(e for _, e in cleanup) else 0


if __name__ == "__main__":
    sys.exit(main())
