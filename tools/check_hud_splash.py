#!/usr/bin/env python3
"""Boot splash: the real H.O.M.E logo, animated (shake + glitch) in place.

hud_draw_splash draws the actual bitmap_home_logo (the firmware's 160x160
PUEO_LOGO_BITMAP, copied into hud/home_logo.h) and animates it with a decaying
shake + row-tear distortion, then holds it steady; setup() runs it at boot.
Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
PG  = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
LOGO = SK / "home_logo.h"
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("the real logo asset is present (bitmap_home_logo, 160x160)",
       LOGO.exists() and "bitmap_home_logo" in LOGO.read_text(encoding="utf-8", errors="replace")
       and "HOME_LOGO_W 160" in LOGO.read_text(encoding="utf-8", errors="replace"))
    ok("splash draws the real logo (not a hand-drawn skull)",
       "hud_bitmap1(" in PG and "bitmap_home_logo" in PG
       and "hud_disc(cx, sy" not in PG)
    ok("splash is ANIMATED: a timed loop with a decaying shake",
       re.search(r"void hud_draw_splash\(\).*?while \(millis\(\) - t0 <.*?random\(-amp", PG, re.S) is not None)
    ok("distortion is a row tear (glitch amp passed to the bitmap draw)",
       re.search(r"hud_bitmap1\([^;]*bitmap_home_logo[^;]*amp\)", PG, re.S) is not None)
    ok("it settles to a steady official card after the shake",
       re.search(r"steady official.*?hud_bitmap1\([^;]*bitmap_home_logo[^;]*, 0\).*?splash_info\(\)", PG, re.S) is not None)
    ok("official info block shows product + build + VERSION + byline",
       "HUD_PRODUCT" in PG and "HUD_VERSION" in PG and "HUD_BUILD" in PG and "HUD_AUTHOR" in PG)
    ok("setup runs the splash at boot",
       "hud_draw_splash();" in INO)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
