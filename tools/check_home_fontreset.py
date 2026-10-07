#!/usr/bin/env python3
"""The home redraw resets the text size, so no feature's font bleeds in.

A feature screen (File Transfer uses PUEO_BODY_SIZE=2) leaves TFT's text-size
multiplier elevated. drawString's font argument does NOT reset that multiplier,
so when the home repaints on exit the tile labels render at 2x -- the "glitch
bigger" the owner saw. The fix: drawPlaybookHome() (and the clock tick, drawn
independently on a timer) force setTextSize(1) first. This pins that so a future
edit can't drop it. Reads source; needs no board.

    python tools/check_home_fontreset.py
"""
import re, sys
from pathlib import Path

INO = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"
FAILED = []

def ok(name, cond):
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond: FAILED.append(name)

def body(src, fn):
    # crude: text from the function signature to the next top-level '}\n' after it
    m = re.search(r"static void %s\(\s*\)\s*\{" % re.escape(fn), src)
    if not m: return ""
    i = m.end(); depth = 1
    while i < len(src) and depth:
        if src[i] == '{': depth += 1
        elif src[i] == '}': depth -= 1
        i += 1
    return src[m.end():i]

def main():
    src = INO.read_text(encoding="utf-8", errors="replace")
    ok("ESP32-DIV.ino exists", bool(src))
    home = body(src, "drawPlaybookHome")
    clock = body(src, "drawHeaderClock")
    ok("drawPlaybookHome resets setTextSize(1)", "setTextSize(1)" in home)
    ok("drawHeaderClock resets setTextSize(1)", "setTextSize(1)" in clock)
    print()
    if FAILED:
        print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0

if __name__ == "__main__":
    sys.exit(main())
