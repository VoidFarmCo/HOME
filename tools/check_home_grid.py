#!/usr/bin/env python3
"""The home tiles draw and hit-test with the same column count.

The purpose-tile home is laid out in drawPlaybookHome() and tapped in the
hit-test loop; both call pbTileRect with a column count. If those two disagree
the tiles render in one grid but taps resolve against another -- you press
'Chat' and launch 'Disrupt'. Route both through one HOME_TILE_COLS constant so
they cannot drift, and pin it to the 2-wide grid. Reads source; needs no board.

    python tools/check_home_grid.py
"""
import re
import sys
from pathlib import Path

INO = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"
CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    src = INO.read_text(encoding="utf-8", errors="replace")

    m = re.search(r"HOME_TILE_COLS\s*=\s*(\d+)", src)
    ok("HOME_TILE_COLS is defined", m is not None)
    ok("HOME_TILE_COLS == 2 (a grid, not a single column)",
       m is not None and int(m.group(1)) == 2)

    # drawPlaybookHome's home branch must use the constant, not a literal.
    draw = re.search(r"static void drawPlaybookHome\(\).*?\n\}", src, re.S)
    ok("drawPlaybookHome uses HOME_TILE_COLS",
       draw is not None and "HOME_TILE_COLS" in draw.group(0))

    # The home hit-test must use the SAME constant. Anchor on the tap handler
    # that sets the group (s_group = g) -- that is the home branch, not a group.
    hit = re.search(r"pbTileRect\(vis, visCount, (\w+), tx, ty, tw, th\);.*?s_group = g;",
                    src, re.S)
    ok("home hit-test uses HOME_TILE_COLS",
       hit is not None and hit.group(1) == "HOME_TILE_COLS")

    ok("purpose tiles have an icon map (kGroupIcon)",
       re.search(r"kGroupIcon\[GRP_COUNT\]", src) is not None)
    # the home draw passes a per-group icon to drawTile, and drawTile renders it
    ok("home tiles pass their icon to drawTile",
       re.search(r"drawTile\([^;]*kGroupIcon\[g\]\)", src) is not None)
    ok("drawTile renders the tile icon (drawBitmapScaled)",
       "drawBitmapScaled" in src and
       re.search(r"static void drawTile\([^)]*const unsigned char\* icon", src) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
