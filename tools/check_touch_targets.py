#!/usr/bin/env python3
"""Every tap target is on the panel, and big enough for a finger.

Two failures, both of which shipped.

A hit box reads like a coordinate and behaves like a dimension, so it
survives a panel change that every drawing call notices. The tile grids were
caught once: they used 100x60, which is the 2.8" tile, and on the 3.5" two
thirds of every tile did not answer a tap. The list menus had the same thing
and were not caught. Five of them tested x <= 220 on a panel 320 across, so
the right 100 px of every row was dead.

And a target can be on the panel and still be unusable. This one is 165 ppi,
so a pixel is 0.154 mm and the 30 px rows are 4.6 mm tall against a 7 mm
minimum for a finger. That is not a bug in the sense that something is
mispositioned; it is a decision, and this prints it rather than failing on
it, because changing a row pitch is a design change and not a fix.

    python tools/check_touch_targets.py

Reads source; needs no board.

What is checked
---------------
Any `button_x2`, `button_y2` or equivalent literal that exceeds the panel, or
that stops short of it by more than a plausible margin. Short of it is the
interesting direction: too wide is clipped by the touch driver and costs
nothing, while too narrow is a strip of screen that looks live and is not.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "ESP32-DIV"

_shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
W = int(re.search(r"#define\s+PUEO_SCREEN_W\s+(\d+)", _shared).group(1))
H = int(re.search(r"#define\s+PUEO_SCREEN_H\s+(\d+)", _shared).group(1))

# 3.5in diagonal at 320x480 is 165 ppi, so a pixel is 0.154 mm.
MM_PER_PX = 25.4 / (H / (3.5 / (1 + (W / float(H)) ** 2) ** 0.5))
FINGER_MM = 7.0

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


def strip_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def main():
    print("panel %dx%d, %.3f mm per pixel, finger minimum %.0f mm = %.0f px"
          % (W, H, MM_PER_PX, FINGER_MM, FINGER_MM / MM_PER_PX))
    print()

    bad_x, bad_y, narrow = [], [], []
    for p in sorted(SKETCH.glob("*.ino")) + sorted(SKETCH.glob("*.cpp")):
        src = strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        for m in re.finditer(r"\bbutton_x2\s*=\s*(\d+)\s*;", src):
            v = int(m.group(1))
            ln = src[:m.start()].count("\n") + 1
            if v > W:
                bad_x.append((p.name, ln, v, "past the right edge"))
            elif v < W - 40:
                bad_x.append((p.name, ln, v,
                              "%d px of every row is dead" % (W - v)))
        for m in re.finditer(r"\bbutton_y2\s*=\s*(\d+)\s*;", src):
            v = int(m.group(1))
            ln = src[:m.start()].count("\n") + 1
            if v > H:
                bad_y.append((p.name, ln, v, "past the bottom edge"))
        # row pitch, where a list spaces its items by a literal
        for m in re.finditer(r"yPos\s*\+\s*(\d+)\s*;", src):
            v = int(m.group(1))
            ln = src[:m.start()].count("\n") + 1
            if v * MM_PER_PX < FINGER_MM:
                narrow.append((p.name, ln, v, v * MM_PER_PX))

    for f, ln, v, why in bad_x:
        print("    %-18s:%-5d button_x2 = %-5d %s" % (f, ln, v, why))
    ok("every row hit box spans the panel", not bad_x,
       "%d do not" % len(bad_x))

    for f, ln, v, why in bad_y:
        print("    %-18s:%-5d button_y2 = %-5d %s" % (f, ln, v, why))
    ok("and none runs off the bottom", not bad_y, "%d do" % len(bad_y))

    # Reported, not failed. A row pitch is a design decision.
    print()
    if narrow:
        print("tap targets below %.0f mm, which is a decision rather than a "
              "fault:" % FINGER_MM)
        seen = set()
        for f, ln, v, mm in narrow:
            if (f, v) in seen:
                continue
            seen.add((f, v))
            print("    %-18s %2d px = %.2f mm" % (f, v, mm))
        print("    (%d sites)" % len(narrow))
    else:
        print("every tap target is at least %.0f mm" % FINGER_MM)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("A hit box is a dimension wearing a coordinate's clothes. It")
        print("does not move when the panel does, and nothing on screen looks")
        print("wrong when it is stale.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
