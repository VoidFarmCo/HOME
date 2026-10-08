#!/usr/bin/env python3
"""Guided touch calibration: visible targets, computed fit, saved to flash.

The owner needs to SEE where to press (past feedback: "i dont see dots"), so cal
draws 5 on-screen targets and captures a raw tap at each, fits sx=ax*rawX+bx /
sy=ay*rawY+by, and stores the coefficients in NVS so the calibration survives a
reboot (no reflash). touch_now uses the saved map; cal auto-runs when unset.
Reads source; no board.
"""
import re, sys
from pathlib import Path
INO = (Path(__file__).resolve().parent.parent / "hud" / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("5 on-screen calibration targets are defined",
       "CAL_N 5" in INO and "s_cal_tx[CAL_N]" in INO and "s_cal_ty[CAL_N]" in INO)
    ok("cal draws a visible target + prompt (so the user knows where to press)",
       re.search(r"void cal_draw\(\).*?hud_ring\(.*?TAP TARGET", INO, re.S) is not None)
    ok("each tap captures the raw reading at that target",
       re.search(r"void cal_capture\([^)]*\).*?s_cal_rx\[s_cal_idx\]", INO, re.S) is not None)
    ok("a linear fit is computed (ax/bx, ay/by)",
       "void cal_compute()" in INO and "s_ax =" in INO and "s_ay =" in INO)
    ok("the calibration is PERSISTED to flash (survives reboot)",
       "Preferences" in INO and "putFloat" in INO
       and re.search(r"void cal_capture\([^)]*\).*?cal_save\(\)", INO, re.S) is not None)
    ok("touch_now uses the saved calibrated map",
       re.search(r"if \(s_cal_valid\).*?s_ax \* rawX \+ s_bx", INO, re.S) is not None)
    ok("calibration auto-runs when none is saved",
       re.search(r"if \(!s_cal_valid \|\| held\).*?s_cal_mode = true", INO, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
