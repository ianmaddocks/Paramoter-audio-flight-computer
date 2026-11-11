#!/usr/bin/env python3
"""Generate the vario's app icon: src/comms/AppIcon.h (a 180 x 180 PNG as a byte array).

The icon is what an iPhone uses when the web page is added to the Home Screen
(Safari: Share > Add to Home Screen), and the page's favicon. It is a simple parachute / paraglider
drawing: a black-outlined canopy in three panels (magenta centre, teal sides), four suspension lines,
and a lime pilot pod, on white. The colours are taken from the glider artwork the project uses.

Standard library only:  python tools/make_icon.py   (takes a few seconds; also writes a preview PNG
next to the header when run with --preview)
"""
import math
import os
import struct
import sys
import zlib

SIZE = 180
SS = 3  # supersampling per axis (anti-aliasing)
WHITE = (255, 255, 255)
BLACK = (20, 20, 24)
MAGENTA = (218, 49, 146)
TEAL = (9, 154, 167)
LIME = (179, 216, 42)

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
OUT = os.path.join(ROOT, "src", "comms", "AppIcon.h")

# ---- the drawing, in design units: 100 wide, 108 high
W, H = 100.0, 108.0
STROKE = 2.0  # half the line width
DISC_C, DISC_R = (50.0, 50.0), 50.0            # the canopy's outer circle
ELL_C, ELL_A, ELL_B = (50.0, 58.0), 49.0, 25.5  # the canopy's underside: an ellipse arc
DIVIDER = ((33.5, 3.0), (18.0, 17.0), (25.5, 36.0))  # left panel divider (quadratic curve); the right is its mirror
LINES = (((33.2, 35.0), (45.2, 91.0)), ((66.8, 35.0), (54.8, 91.0)),   # inner suspension lines
         ((2.5, 58.0), (38.0, 91.0)), ((97.5, 58.0), (62.0, 91.0)))     # outer suspension lines
POD_C, POD_HW, POD_HH = (50.0, 99.7), 21.1, 8.1  # the pilot pod: a rounded bar


def quad(p, t):
    a, b, c = p
    u = 1 - t
    return (u * u * a[0] + 2 * u * t * b[0] + t * t * c[0], u * u * a[1] + 2 * u * t * b[1] + t * t * c[1])


DIV_PTS = [quad(DIVIDER, i / 40.0) for i in range(41)]
DIV_PTS_R = [(W - x, y) for x, y in DIV_PTS]


def divider_x(y):
    """x of the left divider at height y (the curve is monotonic in y)."""
    if y <= DIV_PTS[0][1]:
        return DIV_PTS[0][0]
    if y >= DIV_PTS[-1][1]:
        return DIV_PTS[-1][0]
    for (x0, y0), (x1, y1) in zip(DIV_PTS, DIV_PTS[1:]):
        if y0 <= y <= y1:
            return x0 + (x1 - x0) * (y - y0) / (y1 - y0) if y1 > y0 else x0
    return DIV_PTS[-1][0]


def seg_dist(px, py, a, b):
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    t = max(0.0, min(1.0, ((px - ax) * dx + (py - ay) * dy) / (dx * dx + dy * dy)))
    return math.hypot(px - (ax + t * dx), py - (ay + t * dy))


def poly_dist(px, py, pts):
    return min(seg_dist(px, py, a, b) for a, b in zip(pts, pts[1:]))


def underside_y(x):
    u = (x - ELL_C[0]) / ELL_A
    return ELL_C[1] - ELL_B * math.sqrt(1 - u * u) if abs(u) < 1 else ELL_C[1]


def colour_at(x, y):
    c = WHITE
    r = math.hypot(x - DISC_C[0], y - DISC_C[1])
    yu = underside_y(x)
    # canopy panels
    if r <= DISC_R and y <= yu:
        xl = divider_x(y)
        c = MAGENTA if xl <= x <= W - xl else TEAL
    black = False
    # outer outline of the canopy
    if DISC_R - 2 * STROKE <= r <= DISC_R and y <= yu + STROKE:
        black = True
    # underside of the canopy
    if not black and r <= DISC_R and y <= ELL_C[1] + STROKE:
        ex, ey = (x - ELL_C[0]) / ELL_A, (y - ELL_C[1]) / ELL_B
        e = math.hypot(ex, ey)
        if e > 1e-6:
            gx, gy = ex / (ELL_A * e), ey / (ELL_B * e)
            if abs(e - 1) / math.hypot(gx, gy) <= STROKE:
                black = True
    # panel dividers
    if not black and y < 40 and (x < 40 or x > 60):
        if (x < 40 and poly_dist(x, y, DIV_PTS) <= STROKE) or (x > 60 and poly_dist(x, y, DIV_PTS_R) <= STROKE):
            black = True
    # suspension lines
    if not black and y > 30:
        for a, b in LINES:
            if seg_dist(x, y, a, b) <= STROKE:
                black = True
                break
    if black:
        c = BLACK
    # the pilot pod (over the line ends)
    dx = max(abs(x - POD_C[0]) - (POD_HW - POD_HH), 0.0)
    d = math.hypot(dx, y - POD_C[1])
    if d <= POD_HH:
        c = BLACK if d > POD_HH - 2 * STROKE else LIME
    return c


def render():
    s = SIZE * 0.92 / H  # pixels per design unit
    ox, oy = (SIZE - W * s) / 2, (SIZE - H * s) / 2
    rows = []
    for py in range(SIZE):
        row = []
        for px in range(SIZE):
            r = g = b = 0
            for sy in range(SS):
                for sx in range(SS):
                    x = (px + (sx + 0.5) / SS - ox) / s
                    y = (py + (sy + 0.5) / SS - oy) / s
                    cr, cg, cb = colour_at(x, y) if 0 <= x <= W and 0 <= y <= H else WHITE
                    r += cr
                    g += cg
                    b += cb
            n = SS * SS
            row += [round(r / n), round(g / n), round(b / n)]
        rows.append(row)
    return rows


def png(width, height, rows):
    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    raw = b"".join(b"\x00" + bytes(row) for row in rows)  # filter type 0 for every row
    return (b"\x89PNG\r\n\x1a\n"
            + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))  # 8-bit RGB
            + chunk(b"IDAT", zlib.compress(raw, 9))
            + chunk(b"IEND", b""))


def main():
    data = png(SIZE, SIZE, render())
    lines = []
    for i in range(0, len(data), 16):
        lines.append("    " + ", ".join("0x%02X" % b for b in data[i:i + 16]) + ",")
    text = ("// Generated by tools/make_icon.py - do not edit by hand.\n"
            "// The vario's app icon (PNG, %d x %d, %d bytes): the Home Screen icon and the favicon.\n"
            "#pragma once\n#include <pgmspace.h>\n#include <stdint.h>\n\n"
            "static const uint8_t kAppIcon[] PROGMEM = {\n%s\n};\n" % (SIZE, SIZE, len(data), "\n".join(lines)))
    with open(OUT, "w", newline="\n") as f:
        f.write(text)
    print("wrote %s (%d bytes of PNG)" % (OUT, len(data)))
    if "--preview" in sys.argv:
        prev = os.path.join(ROOT, "icon_preview.png")
        with open(prev, "wb") as f:
            f.write(data)
        print("preview:", prev)


if __name__ == "__main__":
    main()
