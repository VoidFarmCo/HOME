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

    # Tile labels draw in font 2, not the ~3.5pt font 1 that is unreadable at
    # arm's length on this 165 ppi panel. Both drawString(label,...) calls in
    # drawTile must pass font 2 and none may pass font 1.
    body = re.search(r"static void drawTile\(.*?\n\}", src, re.S)
    labels = re.findall(r"drawString\(label,[^;]*,\s*(\d+)\)",
                        body.group(0) if body else "")
    ok("tile labels use font 2 (readable), not font 1",
       len(labels) >= 1 and all(f == "2" for f in labels))

    # Tile labels/icons draw with an auto-contrast ink: accentInk(accent) picks
    # black or white by the accent's luminance, so a light accent (Tan) gets
    # black text and a dark one (Purple) gets white -- never an invisible tile.
    bodytext = body.group(0) if body else ""
    ok("tile ink is auto-contrast (accentInk)",
       "accentInk(accent)" in bodytext and
       re.search(r"setTextColor\(ink,\s*accent\)", bodytext) is not None)
    ok("tile icon uses the same auto-contrast ink",
       re.search(r"drawBitmapScaled\([^;]*,\s*ink,\s*sc\)", bodytext) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
