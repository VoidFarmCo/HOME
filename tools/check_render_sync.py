#!/usr/bin/env python3
"""The screenshots on the website are drawn from the firmware's own numbers.

tools/render_screens.py is not a mockup. It parses the firmware's bitmaps and
TFT_eSPI's fonts and replays the drawing calls at the coordinates the source
gives -- which is what makes the pictures evidence rather than illustration.

It cannot include the firmware's headers to get those coordinates. It is
Python, the constants are C, and half of them sit behind a preprocessor
branch. So it carries its own copy, and a copy is a thing that drifts.

Nothing would tell you when it did. The renderer would keep producing
plausible screenshots, the pictures on the site would keep looking like the
device, and they would be showing a layout the firmware had stopped using.
That is worse than no pictures: a wrong screenshot is believed.

This compares the two sides, constant by constant, for both panels. Every
entry below is a number that exists twice on purpose.

    python tools/check_render_sync.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"
RENDER = REPO / "tools" / "render_screens.py"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def c_panel_consts(src, names, defines=None):
    """{name: (value_35, value_28)} for constants defined once per panel.

    Two things this has to know, both learned by getting them wrong.

    The panel is not always spelled the same way. shared.h guards on
    `#if PUEO_PANEL_35`; the menu grid in the sketch guards on
    `#if TFT_WIDTH >= 320`, which is the same question asked of the panel's
    width. Both mean "the 3.5 inch arm".

    And a value is not always a literal. The 2.8" status bar is
    `#define PUEO_STATUS_TALL PUEO_STATUS_SHORT` -- it is the short bar,
    named rather than repeated. One level of that is resolved from `defines`.

    The branch is tracked rather than assumed to come first. It does today,
    and an #else that swapped places would otherwise compare each panel
    against the other one's numbers and pass, which is the exact failure
    this file exists to stop.
    """
    defines = defines or {}
    out = {n: [None, None] for n in names}
    branch = None          # 0 = the 3.5" arm, 1 = the 2.8" arm
    depth = 0
    for line in src.splitlines():
        t = line.strip()
        if re.match(r"#if\s+PUEO_PANEL_35\b", t) or \
           re.match(r"#if\s+TFT_WIDTH\s*>=\s*320\b", t):
            branch, depth = 0, 1
            continue
        if branch is not None:
            if t.startswith("#if"):
                depth += 1
                continue
            if t.startswith("#else") and depth == 1:
                branch = 1
                continue
            if t.startswith("#endif"):
                depth -= 1
                if depth == 0:
                    branch = None
                continue
        if branch is None:
            continue
        m = re.match(r"(?:static\s+)?(?:const|constexpr)\s+u?int\w*\s+(\w+)"
                     r"\s*=\s*(-?\d+)\s*;", t)
        if not m:
            m = re.match(r"#define\s+(\w+)\s+(-?\w+)\s*$", t)
        if m and m.group(1) in out:
            v = m.group(2)
            if not v.lstrip("-").isdigit():
                v = defines.get(v)          # one level of indirection
                if v is None:
                    continue
            out[m.group(1)][branch] = int(v)
    return {k: tuple(v) for k, v in out.items()}


def c_plain_defines(src, names):
    """Unguarded `#define NAME <int>`, for resolving the indirection above."""
    out = {}
    for n in names:
        m = re.search(r"^#define\s+%s\s+(-?\d+)\s*$" % n, src, re.M)
        if m:
            out[n] = int(m.group(1))
    return out


def py_panel_consts(src, names):
    """The same, out of set_panel()'s `if panel == 35:` / `else:` arms."""
    body = src[src.index("def set_panel("):]
    body = body[:body.index("\n\nX_OFFSET_LEFT")]
    arm35 = body[body.index("if panel == 35:"):body.index("    else:")]
    arm28 = body[body.index("    else:"):]
    out = {}
    for n in names:
        vals = []
        for arm in (arm35, arm28):
            got = None
            # `TILE_W, TILE_H, COLUMN_WIDTH = 145, 92, 155`
            for m in re.finditer(r"^\s*([\w,\s]+?)\s*=\s*([^\n]+)$", arm, re.M):
                keys = [k.strip() for k in m.group(1).split(",")]
                vv = [v.strip() for v in m.group(2).split(",")]
                if n in keys and len(keys) == len(vv):
                    v = vv[keys.index(n)]
                    if v.lstrip("-").isdigit():
                        got = int(v)
                        break
            vals.append(got)
        out[n] = tuple(vals)
    return out


def main():
    ino = read(SKETCH / "ESP32-DIV.ino")
    shared = read(SKETCH / "shared.h")
    utils = read(SKETCH / "utils.cpp")
    spotter = read(SKETCH / "Spotter.cpp")
    drone = read(SKETCH / "DroneScan.cpp")
    rend = read(RENDER)

    print("the menu grid, per panel:")
    grid = ["TILE_W", "TILE_H", "COLUMN_WIDTH", "Y_START", "Y_SPACING",
            "TILE_ICON_DY", "TILE_TEXT_DY"]
    c = c_panel_consts(ino, grid)
    p = py_panel_consts(rend, grid)
    for n in grid:
        ok('  %-13s 3.5" %s   2.8" %s' % (n, c[n][0], c[n][1]),
           c[n] == p[n] and None not in c[n],
           "firmware %s, renderer %s" % (c[n], p[n]))

    print("\nthe chrome, per panel:")
    chrome = ["PUEO_STATUS_TALL", "PUEO_TILE_ICON"]
    c2 = c_panel_consts(shared, chrome,
                        c_plain_defines(shared, ["PUEO_STATUS_SHORT"]))
    for cname, pname in (("PUEO_STATUS_TALL", "STATUS_TALL"),
                         ("PUEO_TILE_ICON", "TILE_ICON")):
        m = re.search(r"%s\s*=\s*(\d+)\s+if\s+panel\s*==\s*35\s+else\s+(\d+)"
                      % pname, rend)
        got = (int(m.group(1)), int(m.group(2))) if m else None
        ok('  %-18s 3.5" %s   2.8" %s' % (cname, c2[cname][0], c2[cname][1]),
           got is not None and got == c2[cname],
           "firmware %s, renderer %s" % (c2[cname], got))

    print("\nthe panel itself:")
    m = re.search(r"W,\s*H\s*=\s*\(320,\s*480\)\s*if\s*panel\s*==\s*35"
                  r"\s*else\s*\(240,\s*320\)", rend)
    ok("  320x480 / 240x320", m is not None,
       "the renderer's canvas no longer matches the two panels")

    print("\nshowFeatureMark(), which every feature opens on:")
    body = utils[utils.index("void showFeatureMark("):]
    body = body[:body.index("\n}")]
    cm = re.search(r"PUEO_SCREEN_H - PUEO_MARK_H\) / 2 - (\d+)", body)
    cc = re.search(r"y \+ PUEO_MARK_H \+ (\d+)", body)
    rm = re.search(r"y = \(H - MARK\) // 2 - (\d+)", rend)
    rc = re.search(r"y \+ MARK \+ (\d+)", rend)
    ok("  the mark is lifted by the same amount",
       bool(cm and rm and cm.group(1) == rm.group(1)),
       "firmware -%s, renderer -%s" % (cm.group(1) if cm else "?",
                                       rm.group(1) if rm else "?"))
    ok("  the caption sits the same distance below it",
       bool(cc and rc and cc.group(1) == rc.group(1)),
       "firmware +%s, renderer +%s" % (cc.group(1) if cc else "?",
                                       rc.group(1) if rc else "?"))

    print("\nthe two full-screen alerts:")
    # Both place mark-plus-text as one block. That arithmetic moved once
    # already, and the renderer had to be moved with it by hand.
    for name, src in (("dwellAlert", spotter), ("droneAlert", drone)):
        blk = re.search(r"kBlockH = mh \+ (\d+)", src)
        two = re.search(r"\(PUEO_SCREEN_H - kBlockH\) \* 2 / 3", src)
        ok("  %s: block is mh + %s" % (name, blk.group(1) if blk else "?"),
           blk is not None)
        ok("  %s: two thirds above, one below" % name, two is not None)
    rblk = re.findall(r"y = \(H - \(mh \+ (\d+)\)\) \* 2 // 3", rend)
    ok("  the renderer places both the same way", len(rblk) == 2,
       "found %d of 2" % len(rblk))
    cblk = re.search(r"kBlockH = mh \+ (\d+)", spotter)
    ok("  and by the same number",
       cblk is not None and all(v == cblk.group(1) for v in rblk),
       "firmware +%s, renderer %s" % (cblk.group(1) if cblk else "?", rblk))

    # The four text lines under the mark, which is where the wording lives.
    want = ["6", "26", "46", "64"]
    for name, src in (("dwellAlert", spotter), ("droneAlert", drone)):
        offs = re.findall(r"y \+ mh \+ (\d+)", src)
        ok("  %s: four lines at %s" % (name, offs), offs == want,
           "expected %s" % want)
    roffs = re.findall(r"y \+ mh \+ (\d+)", rend)
    ok("  the renderer draws them at the same offsets",
       roffs == want * 2, str(roffs))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("A number moved on one side and not the other. The renderer is")
        print("what the website shows; left alone it will keep drawing the")
        print("layout the firmware used to have.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
