"""Check that the owl on the lid is the size it is supposed to be.

This exists because a picture could not tell. The first version of owl2d()
scaled the imported SVG by height/474, which would be right if OpenSCAD
imported a pixel as a unit. It does not -- it converts px to mm at a fixed
96 dpi -- so the owl came out 7.9 mm instead of 30 and sat 11 mm off centre.
Rendered at lid scale that looked entirely plausible. Nothing about the
image said "this is a quarter of the size it should be".

What caught it was arithmetic on the removed volume, so that is what this
automates: extrude the artwork, measure it, and compare against numbers
derived from the source bitmap rather than from the model.

Needs OpenSCAD on PATH or at the usual Windows location. Skips cleanly when
it is absent, because the firmware build does not depend on it.

    python tools/check_logo_scale.py
"""
import os
import re
import subprocess
import sys
import tempfile

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
SCAD = os.path.join(REPO, "docs", "pueo", "pueo-enclosure.scad")
SVG = os.path.join(REPO, "docs", "pueo", "pueo-owl.svg")

CANDIDATES = [
    "openscad",
    r"C:\Program Files\OpenSCAD\openscad.exe",
    r"C:\Program Files (x86)\OpenSCAD\openscad.exe",
    "/usr/bin/openscad",
    "/Applications/OpenSCAD.app/Contents/MacOS/OpenSCAD",
]

TOL_MM = 0.05      # bbox and centre, in mm
TOL_AREA = 0.01    # area, as a fraction


def find_openscad():
    for c in CANDIDATES:
        if os.path.sep in c or (os.altsep and os.altsep in c):
            if os.path.isfile(c):
                return c
        else:
            from shutil import which
            p = which(c)
            if p:
                return p
    return None


def svg_ink_area():
    """Even-odd area of the artwork, in square SVG pixels, plus its box.

    Reuses the tracer's own rasteriser, which is the one already proven
    against the source bitmap to 0.02%.
    """
    sys.path.insert(0, HERE)
    from trace_logo import rasterise

    svg = open(SVG, encoding="utf-8").read()
    m = re.search(r'viewBox="0 0 (\d+) (\d+)"', svg)
    w, h = int(m.group(1)), int(m.group(2))
    d = re.search(r'\sd="([^"]+)"', svg).group(1)

    rings = []
    for sub in d.split("M")[1:]:
        pts = [tuple(float(v) for v in tok.split())
               for tok in re.findall(r"-?\d+\.\d+ -?\d+\.\d+",
                                     sub.replace("Z", ""))]
        if len(pts) >= 3:
            rings.append(pts)

    grid = rasterise(rings, w, h)
    ink = sum(1 for row in grid for v in row if v)
    return ink, w, h


def load_stl(path):
    tris, cur = [], []
    with open(path, encoding="utf-8", errors="replace") as f:
        for line in f:
            line = line.strip()
            if line.startswith("vertex"):
                cur.append(tuple(float(v) for v in line.split()[1:4]))
                if len(cur) == 3:
                    tris.append(tuple(cur))
                    cur = []
    return tris


def volume(tris):
    t = 0.0
    for a, b, c in tris:
        t += (a[0] * (b[1] * c[2] - c[1] * b[2])
              - a[1] * (b[0] * c[2] - c[0] * b[2])
              + a[2] * (b[0] * c[1] - c[0] * b[1])) / 6.0
    return abs(t)


def render(exe, scad_text, tmpdir, name, defines=()):
    src = os.path.join(tmpdir, name + ".scad")
    out = os.path.join(tmpdir, name + ".stl")
    with open(src, "w", encoding="utf-8", newline="\n") as f:
        f.write(scad_text)
    cmd = [exe]
    for d in defines:
        cmd += ["-D", d]
    cmd += ["-o", out, src]
    r = subprocess.run(cmd, capture_output=True, text=True)
    if not os.path.isfile(out):
        print(r.stderr[-2000:], file=sys.stderr)
        raise SystemExit("openscad produced no output for %s" % name)
    return load_stl(out)


checks = 0


def case(name, cond, detail=""):
    global checks
    if not cond:
        raise AssertionError("FAILED: %s %s" % (name, detail))
    checks += 1
    print("  ok  %s %s" % (name, detail))


def main():
    exe = find_openscad()
    if exe is None:
        print("openscad not found - skipping (this check is optional)")
        return 0
    print("openscad : %s" % exe)

    ink, pw, ph = svg_ink_area()
    print("artwork  : %d x %d px, %d ink px (%.1f%% coverage)"
          % (pw, ph, ink, 100.0 * ink / (pw * ph)))

    tmp = tempfile.mkdtemp(prefix="pueo-logo-")
    scad_dir = os.path.dirname(SCAD).replace("\\", "/")

    for height in (20.0, 30.0, 45.0):
        probe = ('use <%s/pueo-enclosure.scad>\n'
                 'linear_extrude(1) owl2d(%g);\n' % (scad_dir, height))
        tris = render(exe, probe, tmp, "probe%g" % height)
        vs = [p for t in tris for p in t]
        xs = [p[0] for p in vs]
        ys = [p[1] for p in vs]
        bw, bh = max(xs) - min(xs), max(ys) - min(ys)
        cx, cy = (max(xs) + min(xs)) / 2.0, (max(ys) + min(ys)) / 2.0
        area = volume(tris)          # extruded 1 mm, so volume == area

        want_h = height
        want_w = height * pw / float(ph)
        want_a = ink * (height / float(ph)) ** 2

        print("owl2d(%g):" % height)
        case("height", abs(bh - want_h) < TOL_MM,
             "%.2f mm (want %.2f)" % (bh, want_h))
        case("width ", abs(bw - want_w) < TOL_MM,
             "%.2f mm (want %.2f)" % (bw, want_w))
        case("centred", abs(cx) < TOL_MM and abs(cy) < TOL_MM,
             "(%.2f, %.2f)" % (cx, cy))
        case("area  ", abs(area - want_a) / want_a < TOL_AREA,
             "%.1f mm2 (want %.1f)" % (area, want_a))

    # The lid itself: the deboss must remove what the artwork says it will.
    #
    # What this caught was an owl hanging off the bottom of a lid that had got
    # shorter, which the render did not show and no printed part ever would
    # have: LOGO is off by default.
    lid_scad = 'use <%s/pueo-enclosure.scad>\n' % scad_dir
    with open(SCAD, encoding="utf-8") as f:
        text = f.read()
    depth = float(re.search(r"LOGO_DEPTH\s*=\s*([\d.]+)", text).group(1))
    lh = float(re.search(r"LOGO_H\s*=\s*([-\d.]+)", text).group(1))

    print("lid, LOGO_H = %g:" % lh)
    defines = ['PART="lid"']
    on = render(exe, lid_scad + "lid();\n", tmp, "lidon", defines + ["LOGO=true"])
    off = render(exe, lid_scad + "lid();\n", tmp, "lidoff", defines + ["LOGO=false"])
    removed = volume(off) - volume(on)
    want = ink * (lh / float(ph)) ** 2 * depth
    # Clipping is what a volume shortfall means, so say that rather than
    # leaving the next person to work back from a number.
    case("nothing clipped", removed >= want * (1 - TOL_AREA),
         "all of the owl is on the lid")
    case("deboss volume", abs(removed - want) / want < TOL_AREA,
         "%.1f mm3 (want %.1f, at %g mm tall x %g deep)"
         % (removed, want, lh, depth))
    case("LOGO=false is a no-op on volume", volume(off) > volume(on),
         "%.1f vs %.1f mm3" % (volume(off), volume(on)))

    print()
    print("ok -- %d checks" % checks)
    print("the owl is the size the artwork says, not the size the render "
          "suggests")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
