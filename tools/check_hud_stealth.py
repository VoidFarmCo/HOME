#!/usr/bin/env python3
"""NIGHT / STEALTH mode: dim backlight + red night-vision tint.

A settings toggle PWMs the backlight down and rewrites the frame to dim red at
the single present seam (so no page code changes and the green/cyan UI maps to
red *shades* by luminance instead of blanking). Reads source.
"""
import re, sys
from pathlib import Path
H  = Path(__file__).resolve().parent.parent / "hud"
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
PG  = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("backlight is PWM-driven (LEDC), not a bare on/off GPIO",
       "ledcAttach(LCD_BL" in INO and "ledcWrite(LCD_BL" in INO
       and "digitalWrite(LCD_BL, HIGH)" not in INO)
    ok("stealth dims the backlight below full brightness",
       "STEALTH_DUTY" in INO and re.search(r"#define\s+STEALTH_DUTY\s+(\d+)", INO) is not None
       and int(re.search(r"#define\s+STEALTH_DUTY\s+(\d+)", INO).group(1)) < 255)
    # the tint runs at the present seam, maps luminance to the red channel only
    m = re.search(r"static void stealth_tint\([^)]*\)\s*\{(.*?)\n\}", INO, re.S)
    body = m.group(1) if m else ""
    ok("tint maps luminance to RED only (shades, not a blanking mask)",
       ("0x1F" in body and "0x3F" in body) and "<< 11" in body and "lum" in body)
    ok("the tint is applied in hud_present_fb when stealth is on",
       re.search(r"hud_present_fb\([^)]*\)\s*\{[^}]*s_stealth[^}]*stealth_tint", INO, re.S) is not None)
    ok("a SETTINGS toggle switches stealth on/off",
       "hud_set_stealth(!hud_stealth())" in PG and "STEALTH" in PG)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
