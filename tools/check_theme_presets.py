#!/usr/bin/env python3
"""Themes are a named preset table the Settings row cycles through.

The fork shipped a two-way Dark/Light toggle. H.O.M.E turns that into a named
preset list (Dark, Light, Midnight, Matrix, Sand, Contrast...) so one pick sets
the whole base palette, with the accent as a separate knob. This pins: the
palette table drives applyThemeToPalette (not a hard-coded Dark/Light branch),
the table has more than the original two, and the Settings row cycles the full
count both directions (so no preset is unreachable). Reads source; no board.

    python tools/check_theme_presets.py
"""
import re, sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FAILED = []

def ok(name, cond, d=""):
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (" -- " + d if d else "")))
    if not cond: FAILED.append(name)

def main():
    theme  = (SKETCH / "Theme.cpp").read_text(encoding="utf-8", errors="replace")
    shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    utils  = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")

    rows = re.findall(r'\{\s*"([^"]+)"\s*,', theme[theme.find("kThemes"):])
    ok("kThemes preset table found", len(rows) >= 1)
    ok("more than the original two presets", len(rows) >= 4, "found %s" % rows)

    mc = re.search(r"THEME_PRESET_COUNT\s*=\s*(\d+)", shared)
    count = int(mc.group(1)) if mc else -1
    ok("THEME_PRESET_COUNT matches the table", count == len(rows),
       "count=%d table=%d" % (count, len(rows)))

    ok("applyThemeToPalette indexes the table (not a Dark/Light branch)",
       re.search(r"applyThemeToPalette[^}]*kThemes\[", theme, re.S) is not None
       and "if (t == Theme::Light)" not in theme)

    # Settings row cycles the whole count both ways (prev and next).
    cyc = len(re.findall(r"%\s*THEME_PRESET_COUNT", utils))
    ok("theme Settings row cycles all presets (prev+next)", cyc >= 2,
       "found %d %% THEME_PRESET_COUNT" % cyc)
    ok("no hard-coded applyTheme(Theme::Dark/Light) left in the row",
       "applyTheme(Theme::Dark)" not in utils and "applyTheme(Theme::Light)" not in utils)

    print()
    if FAILED:
        print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0

if __name__ == "__main__":
    sys.exit(main())
