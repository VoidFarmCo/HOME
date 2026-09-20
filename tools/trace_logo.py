"""Trace the Pueo owl bitmap into an SVG that OpenSCAD can extrude.

The logo exists as line art on a transparent PNG. OpenSCAD cannot extrude a
bitmap usefully -- surface() would give a 512x512 stepped mesh -- so the art
has to become outlines first. No potrace or Inkscape on this machine, and
adding a toolchain dependency for one file is worse than fifty lines of
marching squares.

What this does:

  1. binarises the PNG on alpha and brightness,
  2. walks marching squares over the 0.5 isocontour, which yields closed
     loops for outer boundaries and for holes alike,
  3. simplifies each loop with Douglas-Peucker,
  4. writes one SVG path with fill-rule="evenodd", so nesting sorts itself
     out and the eyes and letter counters stay holes,
  5. rasterises what it wrote and compares it to the input.

Step 5 is the point. A tracer that silently drops a contour, or fills a
hole, produces an SVG that looks plausible in a viewer and prints wrong, and
the error would not surface until a part came off the bed. The comparison
reports the disagreement as a percentage of the artwork.

    python tools/trace_logo.py [--tolerance 0.6] [--out docs/pueo/pueo-owl.svg]
"""
import argparse
import math
import os
import sys

from PIL import Image

DEFAULT_SRC = (r"C:\Users\jerem\OneDrive\Desktop\magikh0e-website"
               r"\img\pueo-logo.png")
DEFAULT_OUT = "docs/pueo/pueo-owl.svg"


def binarise(path):
    im = Image.open(path).convert("RGBA")
    w, h = im.size
    px = im.load()
    m = [[False] * w for _ in range(h)]
    for y in range(h):
        row = m[y]
        for x in range(w):
            r, g, b, a = px[x, y]
            row[x] = a > 128 and (r + g + b) >= 384
    return m, w, h


def crop(mask, w, h):
    x0, y0, x1, y1 = w, h, -1, -1
    for y in range(h):
        row = mask[y]
        for x in range(w):
            if row[x]:
                if x < x0: x0 = x
                if x > x1: x1 = x
                if y < y0: y0 = y
                if y > y1: y1 = y
    if x1 < 0:
        raise SystemExit("no ink found in the source image")
    out = [[mask[y][x] for x in range(x0, x1 + 1)] for y in range(y0, y1 + 1)]
    return out, x1 - x0 + 1, y1 - y0 + 1


# Marching squares. Corners are TL, TR, BR, BL; midpoints T, R, B, L. Each
# case lists the directed midpoint pairs that make up the contour in that
# cell. Cases 5 and 10 are the ambiguous saddles; either resolution closes,
# and the rasterised check at the end is what confirms the choice was sane.
CASES = {
    0:  [],
    1:  [("L", "B")],
    2:  [("B", "R")],
    3:  [("L", "R")],
    4:  [("R", "T")],
    5:  [("L", "T"), ("R", "B")],
    6:  [("B", "T")],
    7:  [("L", "T")],
    8:  [("T", "L")],
    9:  [("T", "B")],
    10: [("T", "R"), ("B", "L")],
    11: [("T", "R")],
    12: [("R", "L")],
    13: [("R", "B")],
    14: [("B", "L")],
    15: [],
}


def march(mask, w, h):
    """Closed loops over the 0.5 isocontour, in half-pixel integer coords."""
    def at(y, x):
        if 0 <= y < h and 0 <= x < w:
            return mask[y][x]
        return False

    # keys are doubled coordinates so midpoints stay integral
    edges = {}
    for i in range(-1, h):
        for j in range(-1, w):
            a = at(i, j)          # TL
            b = at(i, j + 1)      # TR
            c = at(i + 1, j + 1)  # BR
            d = at(i + 1, j)      # BL
            case = (8 if a else 0) + (4 if b else 0) + \
                   (2 if c else 0) + (1 if d else 0)
            segs = CASES[case]
            if not segs:
                continue
            pt = {
                "T": (2 * j + 1, 2 * i),
                "R": (2 * j + 2, 2 * i + 1),
                "B": (2 * j + 1, 2 * i + 2),
                "L": (2 * j, 2 * i + 1),
            }
            for s, e in segs:
                edges.setdefault(pt[s], []).append(pt[e])

    loops = []
    while edges:
        start = next(iter(edges))
        loop = [start]
        cur = start
        while True:
            nxts = edges.get(cur)
            if not nxts:
                break
            nxt = nxts.pop()
            if not nxts:
                del edges[cur]
            if nxt == start:
                break
            loop.append(nxt)
            cur = nxt
        if len(loop) >= 3:
            loops.append(loop)
    return loops


def perp2(p, a, b):
    """Squared distance from p to segment ab."""
    px, py = p
    ax, ay = a
    bx, by = b
    dx, dy = bx - ax, by - ay
    if dx == 0 and dy == 0:
        return (px - ax) ** 2 + (py - ay) ** 2
    t = ((px - ax) * dx + (py - ay) * dy) / float(dx * dx + dy * dy)
    t = max(0.0, min(1.0, t))
    qx, qy = ax + t * dx, ay + t * dy
    return (px - qx) ** 2 + (py - qy) ** 2


def simplify(pts, tol):
    """Douglas-Peucker, iterative, on a closed ring."""
    if len(pts) < 4:
        return pts
    tol2 = tol * tol
    keep = [False] * len(pts)
    keep[0] = keep[-1] = True
    stack = [(0, len(pts) - 1)]
    while stack:
        lo, hi = stack.pop()
        if hi <= lo + 1:
            continue
        worst, wi = -1.0, -1
        for k in range(lo + 1, hi):
            d = perp2(pts[k], pts[lo], pts[hi])
            if d > worst:
                worst, wi = d, k
        if worst > tol2:
            keep[wi] = True
            stack.append((lo, wi))
            stack.append((wi, hi))
    return [p for p, k in zip(pts, keep) if k]


def rasterise(rings, w, h):
    """Even-odd scanline fill, for checking the trace against the source.

    Coordinates here are the marching-squares ones, in which pixel (x, y)
    has its CENTRE at (x, y) -- the contour runs through cell midpoints, so
    a T edge lands on y = i exactly. Sampling at y + 0.5, as a corner-based
    rasteriser would, offsets every boundary by half a pixel and reports a
    one-pixel error band along the whole perimeter. On line art that is
    several percent of the ink, which looks exactly like a real fault.

    The scanline is nudged by an epsilon because vertices sit on multiples
    of 0.5 and a scanline through a vertex is counted twice or not at all.
    """
    EPS = 1e-7
    grid = [[False] * w for _ in range(h)]
    for y in range(h):
        sy = y + EPS
        xs = []
        for ring in rings:
            n = len(ring)
            for k in range(n):
                x1, y1 = ring[k]
                x2, y2 = ring[(k + 1) % n]
                if (y1 > sy) == (y2 > sy):
                    continue
                xs.append(x1 + (sy - y1) * (x2 - x1) / float(y2 - y1))
        if not xs:
            continue
        xs.sort()
        for k in range(0, len(xs) - 1, 2):
            # pixel centres are integral, so fill the integers spanned
            a = int(math.ceil(xs[k] - EPS))
            b = int(math.floor(xs[k + 1] + EPS))
            for x in range(max(0, a), min(w - 1, b) + 1):
                grid[y][x] = True
    return grid


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--src", default=DEFAULT_SRC)
    ap.add_argument("--out", default=DEFAULT_OUT)
    ap.add_argument("--tolerance", type=float, default=0.4,
                    help="Douglas-Peucker tolerance in source pixels")
    args = ap.parse_args()

    mask, w, h = binarise(args.src)
    mask, w, h = crop(mask, w, h)
    ink = sum(sum(1 for v in row if v) for row in mask)
    print("artwork      : %d x %d px, %d ink px" % (w, h, ink))

    loops = march(mask, w, h)
    raw_pts = sum(len(l) for l in loops)
    print("contours     : %d loops, %d points" % (len(loops), raw_pts))

    # marching-square coords are doubled; halve them into pixel space
    rings = []
    for loop in loops:
        pts = [(x / 2.0, y / 2.0) for x, y in loop]
        pts = simplify(pts, args.tolerance)
        if len(pts) >= 3:
            rings.append(pts)
    kept = sum(len(r) for r in rings)
    print("simplified   : %d loops, %d points (tolerance %.2f px)"
          % (len(rings), kept, args.tolerance))

    # ---- the check ----
    got = rasterise(rings, w, h)
    miss = 0
    for y in range(h):
        gr, mr = got[y], mask[y]
        for x in range(w):
            if gr[x] != mr[x]:
                miss += 1
    pct = 100.0 * miss / float(ink)
    print("fidelity     : %d px differ, %.2f%% of the artwork" % (miss, pct))

    if pct > 3.0:
        print("REFUSING to write: the trace does not match the source.",
              file=sys.stderr)
        return 1

    parts = []
    for r in rings:
        d = "M%.2f %.2f " % r[0]
        d += " ".join("L%.2f %.2f" % p for p in r[1:])
        parts.append(d + " Z")
    svg = (
        '<?xml version="1.0" encoding="UTF-8"?>\n'
        '<!-- Pueo owl, traced from img/pueo-logo.png by tools/trace_logo.py.\n'
        '     Generated: do not hand-edit, re-run the tracer.\n'
        '     fill-rule is evenodd, which is what keeps the eyes and the\n'
        '     counters in PUEO as holes rather than filling them in.\n'
        '     magikh0e.pl -->\n'
        '<svg xmlns="http://www.w3.org/2000/svg" '
        'width="%dpx" height="%dpx" viewBox="0 0 %d %d">\n'
        '<path fill-rule="evenodd" fill="#000000" d="%s"/>\n'
        '</svg>\n'
    ) % (w, h, w, h, " ".join(parts))

    out = args.out
    d = os.path.dirname(out)
    if d and not os.path.isdir(d):
        os.makedirs(d)
    with open(out, "w", encoding="utf-8", newline="\n") as f:
        f.write(svg)
    print("wrote        : %s (%.1f KB)" % (out, len(svg) / 1024.0))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
