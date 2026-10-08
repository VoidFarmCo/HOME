#!/usr/bin/env python3
"""SCAN footer (band counts) is visible, not hidden under the chrome strip.

draw_chrome fills the TOP strip LAST, so any footer drawn into the top strip is
painted over. The scan band-count footer must live at the BOTTOM of the content
band (CONTENT_B - FOOT_H), which chrome's top-strip fill never touches, and the
list must reserve that row. Reads source.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    # the hud_text line that actually draws the band-count footer string `foot`
    tline = next((l for l in PG.splitlines() if "hud_text(" in l and " foot," in l), "")
    ok("the footer TEXT is drawn at the bottom of the content band (CONTENT_B - FOOT_H)",
       "CONTENT_B - FOOT_H" in tline)
    ok("the footer bar is filled at the bottom band too",
       re.search(r"hud_fill_rect\(0, CONTENT_B - FOOT_H, HUD_W, FOOT_H", PG) is not None)
    ok("scan footer is NOT drawn in the top strip (no FOOT_Y)",
       "FOOT_Y" not in PG)
    ok("the scan list reserves the footer row so it isn't overlapped",
       re.search(r"scan_rows\(\)\s*\{\s*return \(CONTENT_B - CONTENT_Y - FOOT_H\)", PG) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
