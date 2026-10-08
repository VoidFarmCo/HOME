#!/usr/bin/env python3
"""Settings gear (top-right header) opens a panel with RECALIBRATE TOUCH.

A gear is drawn in the top status strip; tapping it toggles a modal SETTINGS panel;
the RECALIBRATE TOUCH button calls hud_request_recal(), whose sketch-side seam
re-enters the guided calibration (sets the cal mode). Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
PG  = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("a gear is drawn top-right of the status strip",
       "GEAR_X" in PG and re.search(r"GEAR_X\s*\(HUD_W - 13\)", PG) is not None
       and re.search(r"draw_chrome\([^)]*\)\s*\{.*?hud_disc\(GEAR_X", PG, re.S) is not None)
    ok("tapping the gear toggles the SETTINGS panel",
       re.search(r"y < STAT_H && x > HUD_W - 24.*?s_settings = !s_settings", PG, re.S) is not None)
    ok("the panel shows a RECALIBRATE TOUCH button",
       "void draw_settings()" in PG and "RECALIBRATE TOUCH" in PG)
    ok("the panel is modal (drawn instead of the page when open)",
       re.search(r"if \(s_settings\)\s*\{?\s*draw_settings\(\)", PG) is not None)
    ok("the RECALIBRATE button calls hud_request_recal()",
       re.search(r"SET_BTN_X.*?hud_request_recal\(\)", PG, re.S) is not None)
    ok("the sketch implements the recal seam (re-enters calibration)",
       re.search(r"void hud_request_recal\(\)\s*\{[^}]*s_cal_mode = true", INO) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
