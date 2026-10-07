#!/usr/bin/env python3
"""The home tiles honour the Tile Borders / Tile Labels toggles.

check_settings proves the two bools are persisted and on a Settings row; this
proves they actually gate the draw -- drawTile reads settings().tileBorders /
tileLabels and skips the outline or the caption when they are off. Without this
a future edit could keep the settings wired but draw the tile unconditionally,
and the toggle would silently do nothing. Reads source; no board.

    python tools/check_tile_toggles.py
"""
import re, sys
from pathlib import Path

INO = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"
FAILED = []

def ok(name, cond):
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond: FAILED.append(name)

def main():
    src = INO.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"static void drawTile\(.*?\n\}", src, re.S)
    body = m.group(0) if m else ""
    ok("drawTile found", bool(body))
    ok("drawTile reads tileBorders + tileLabels",
       "settings().tileBorders" in body and "settings().tileLabels" in body)
    # The outline draw is guarded by the borders flag.
    ok("tile outline is gated by the border toggle",
       re.search(r"if\s*\(\s*borders\s*\)\s*tft\.drawRoundRect", body) is not None)
    # Each label draw is guarded by the labels flag.
    ok("tile caption is gated by the label toggle",
       re.search(r"if\s*\(\s*labels\s*\)\s*tft\.drawString\(label", body) is not None
       and re.search(r"else if\s*\(\s*labels\s*\)", body) is not None)
    print()
    if FAILED:
        print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0

if __name__ == "__main__":
    sys.exit(main())
