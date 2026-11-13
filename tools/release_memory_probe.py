#!/usr/bin/env python3
"""Free memory of a RELEASE build while web pages load (finding F9).

The HIL suite needs the test build, which has about 14 kB less free memory than a release build (a 96-line log buffer
instead of 40, test code), so its page-load figures look worse than a flying vario's. This probe measures the real thing:
flash the release build (pio run -e release -t upload), then

    python tools/release_memory_probe.py [host] [browsers]

It restarts the vario, waits, opens the page in 1 to 3 headless browsers at once (a phone, a second phone, a PC), cycles them
through the tabs, and prints the free memory and the lowest free memory since power-on after each step. Pass if the lowest
free memory stays above 15 kB. (Measured on the build of this change: 28.8 kB with three browsers loading at once,
60 kB free once they have loaded.)
"""
import os
import socket
import sys
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import hil_test as h  # noqa: E402

LIMIT = 15000


def main():
    host = sys.argv[1] if len(sys.argv) > 1 else "paravario.local"
    count = int(sys.argv[2]) if len(sys.argv) > 2 else 3
    v = h.Vario(host)
    st = v.status()
    if st.get("test"):
        print("This is the test build: flash the release build first (pio run -e release -t upload).")
        return 2
    h.restart_vario(v, "memory probe")
    time.sleep(10)
    st = v.status()
    print(f"idle after power-on: free {st['heap']} B, lowest {st['heapMin']} B, largest block {st['heapBlock']} B")
    browsers = []
    try:
        for _ in range(count):
            b = h.Browser()
            b.phone()
            b.cmd("Network.enable")
            b.cmd("Network.setExtraHTTPHeaders", headers={"X-HIL": "1"})
            b.cmd("Page.navigate", url="http://" + socket.gethostbyname(host) + "/")
            browsers.append(b)
        time.sleep(12)
        st = v.status()
        print(f"{count} browsers loaded: free {st['heap']} B, lowest {st['heapMin']} B")
        for rnd in range(3):
            for i, b in enumerate(browsers):
                tab = ("settings", "system", "help", "flight")[(rnd + i) % 4]
                b.js(f"document.querySelector('button[data-tab={tab}]').click()")
            time.sleep(8)
            st = v.status()
            print(f"round {rnd + 1}: free {st['heap']} B, lowest {st['heapMin']} B")
    finally:
        for b in browsers:
            b.close()
    ok = st["heapMin"] >= LIMIT
    print(("PASS" if ok else "FAIL") + f": lowest free memory {st['heapMin']} B (limit {LIMIT} B)")
    return 0 if ok else 1


if __name__ == "__main__":
    sys.exit(main())
