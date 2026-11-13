#!/usr/bin/env python3
"""Watches the vario on the network to find out why it drops off.

Every few seconds it asks the vario for its status (by name and by address), and records whether it answered, how long it took,
the vario's uptime, why it last restarted (reset reason), its Wi-Fi signal and channel, how often the Wi-Fi driver reports it was
dropped from the router (and the reason code), free memory, and the battery voltage, together with what this PC's own Wi-Fi is
doing. When the vario stops answering it also tries a ping and looks at the PC's neighbour table, which tells a vario whose
web server is stuck apart from one that has left the network, and a name that does not resolve apart from an address that does
not answer.

  python tools/net_watch.py                      # watch until Ctrl+C, log to results/net_watch.csv
  python tools/net_watch.py --ip 192.168.68.66   # also (or only) probe this address
  python tools/net_watch.py --interval 3

Read the output like this:
  DOWN (name and address)   the vario is not answering at all: look at the ping line (ping OK = web server stuck, no reply = off the
                            network) and the last Wi-Fi reason code it reported before it went.
  DOWN (name only)          the address answers but the name does not: this PC's name lookup (mDNS), not the vario.
  REBOOT                    the uptime went backwards: resetReason says why (power-on = lost power, brownout = low supply voltage,
                            crash / watchdog = a firmware fault, software restart = a settings change or the Restart button).
  DROPS                     the vario's Wi-Fi driver lost its link to the router (staDisc went up) though it kept running; staReason
                            is the standard Wi-Fi reason code (2 auth expired, 8 left, 15 handshake timeout, 200 beacon timeout, 201 AP not
                            found, 202 auth failed, 205 connection failed).
"""
import argparse
import csv
import json
import os
import re
import socket
import subprocess
import time
import urllib.request

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))


def get_status(url, timeout):
    t0 = time.time()
    req = urllib.request.Request(url, headers={"X-HIL": "1", "Cache-Control": "no-store"})  # X-HIL: not counted as a person's browser
    try:
        with urllib.request.urlopen(req, timeout=timeout) as r:
            body = r.read()
        return json.loads(body), time.time() - t0, ""
    except Exception as e:  # noqa: BLE001
        return None, time.time() - t0, type(e).__name__ + ": " + str(e)[:60]


def pc_wifi():
    """This PC's own Wi-Fi: SSID, band/channel, signal (Windows)."""
    try:
        out = subprocess.run(["netsh", "wlan", "show", "interfaces"], capture_output=True, text=True, timeout=8).stdout
    except Exception:  # noqa: BLE001
        return {}
    d = {}
    for key, pat in (("pcSsid", r"^\s*SSID\s*:\s*(.+)$"), ("pcBssid", r"^\s*BSSID\s*:\s*(.+)$"), ("pcSignal", r"^\s*Signal\s*:\s*(.+)$"),
                     ("pcChannel", r"^\s*Channel\s*:\s*(.+)$"), ("pcState", r"^\s*State\s*:\s*(.+)$")):
        m = re.search(pat, out, re.M)
        d[key] = m.group(1).strip() if m else ""
    return d


def ping(host):
    try:
        out = subprocess.run(["ping", "-n", "1", "-w", "1500", host], capture_output=True, text=True, timeout=6).stdout
        return "reply" if "TTL=" in out else "no reply"
    except Exception:  # noqa: BLE001
        return "?"


def neighbour(ip):
    try:
        out = subprocess.run(["arp", "-a", ip], capture_output=True, text=True, timeout=6).stdout
        m = re.search(re.escape(ip) + r"\s+([0-9a-f-]{17})\s+(\w+)", out, re.I)
        return f"{m.group(1)} ({m.group(2)})" if m else "no entry"
    except Exception:  # noqa: BLE001
        return "?"


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--host", default="paravario.local")
    ap.add_argument("--ip", default="")
    ap.add_argument("--interval", type=float, default=5.0)
    ap.add_argument("--timeout", type=float, default=4.0)
    ap.add_argument("--csv", default=os.path.join(ROOT, "results", "net_watch.csv"))
    a = ap.parse_args()
    os.makedirs(os.path.dirname(a.csv), exist_ok=True)
    new = not os.path.exists(a.csv)
    f = open(a.csv, "a", newline="", encoding="utf-8")
    cols = ["time", "state", "via", "latency_s", "error", "uptime", "resetReason", "rssi", "channel", "staConn", "staDisc", "staReason",
            "staDiscAgoS", "apClients", "heap", "heapMin", "battV", "version", "pcSsid", "pcBssid", "pcSignal", "pcChannel", "pcState", "ping"]
    w = csv.DictWriter(f, fieldnames=cols)
    if new:
        w.writeheader()
    state, last_up, last_disc, last_ip, since = None, None, None, a.ip, time.time()
    print(f"watching {a.host}{' and ' + a.ip if a.ip else ''} every {a.interval:g} s; log {a.csv}; Ctrl+C to stop")
    try:
        while True:
            now = time.strftime("%H:%M:%S")
            pcw = pc_wifi()
            try:
                socket.getaddrinfo(a.host, 80)
                name_ok = True
            except OSError:
                name_ok = False
            st, lat, err, via = None, 0.0, "", ""
            if name_ok:
                st, lat, err = get_status(f"http://{a.host}/api/status", a.timeout)
                via = "name"
            if st is None and last_ip:
                st, lat2, err2 = get_status(f"http://{last_ip}/api/status", a.timeout)
                if st is not None:
                    via, lat, err = "address", lat2, "" if name_ok else "name does not resolve"
                elif not name_ok:
                    err = "name does not resolve; address: " + err2
            row = {"time": now, "via": via, "latency_s": f"{lat:.2f}", "error": err, **pcw}
            if st is not None:
                wf = st.get("wifi", {})
                if wf.get("serverIp"):
                    last_ip = wf["serverIp"]
                row.update(state="UP", uptime=st.get("uptime"), resetReason=st.get("resetReason", ""), rssi=wf.get("rssi", ""),
                           channel=wf.get("channel", ""), staConn=wf.get("staConn", ""), staDisc=wf.get("staDisc", ""),
                           staReason=wf.get("staReason", ""), staDiscAgoS=wf.get("staDiscAgoS", ""), apClients=wf.get("apClients", ""),
                           heap=st.get("heap"), heapMin=st.get("heapMin"), battV=st.get("batt", {}).get("v", ""),
                           version=st.get("version", ""))
                if state != "UP":
                    print(f"{now}  UP    via {via}, uptime {st.get('uptime')} s, reset reason: {st.get('resetReason', '?')}, rssi {wf.get('rssi', '?')}"
                          f", channel {wf.get('channel', '?')}, drops {wf.get('staDisc', '?')} (last reason {wf.get('staReason', '?')})"
                          f"{'  after ' + format(time.time() - since, '.0f') + ' s down' if state == 'DOWN' else ''}")
                    if via == "address" and not name_ok:
                        print(f"{now}  NOTE  the address answers but the name does not resolve: this PC's name lookup, not the vario")
                    since = time.time()
                if last_up is not None and st.get("uptime") is not None and st["uptime"] + 3 < last_up:
                    print(f"{now}  REBOOT  uptime {last_up} -> {st['uptime']} s; reset reason: {st.get('resetReason', '?')}")
                if last_disc is not None and wf.get("staDisc", 0) > last_disc:
                    print(f"{now}  DROPS   router link dropped {wf['staDisc'] - last_disc} time(s) (reason code {wf.get('staReason')}, "
                          f"{wf.get('staDiscAgoS')} s ago), rssi {wf.get('rssi')}, heap {st.get('heap')} (lowest {st.get('heapMin')})")
                last_up = st.get("uptime")
                last_disc = wf.get("staDisc", last_disc)
                state = "UP"
            else:
                row.update(state="DOWN")
                target = last_ip or a.host
                row["ping"] = ping(target)
                if state != "DOWN":
                    since = time.time()
                    print(f"{now}  DOWN  {err}; ping {target}: {row['ping']}; neighbour table: {neighbour(last_ip) if last_ip else '-'}; "
                          f"PC Wi-Fi: {pcw.get('pcSsid', '?')} {pcw.get('pcSignal', '?')} ch {pcw.get('pcChannel', '?')}; "
                          f"last seen uptime {last_up}, drops {last_disc}")
                state = "DOWN"
            w.writerow(row)
            f.flush()
            time.sleep(a.interval)
    except KeyboardInterrupt:
        print("stopped")


if __name__ == "__main__":
    main()
