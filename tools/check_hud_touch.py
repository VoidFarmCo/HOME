#!/usr/bin/env python3
"""Touch works: the bus has MISO, the XPT2046 is on it, and taps switch pages.

The SPI bus MUST expose MISO (GPIO 2) or the touch chip returns all-ones and
nothing registers (that bug cost a session). The XPT2046 is a second device on
the display's SPI bus; the loop reads it and routes a tap to hud_on_touch, which
switches the page when a top tab is hit. The raw->screen map is the 5-point
calibration (axes swapped+inverted in landscape). Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
PG  = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("SPI bus exposes MISO 2 (required for touch reads)",
       re.search(r"#define\s+LCD_MISO\s+2\b", INO) is not None)
    ok("XPT2046 added as a device on the display bus",
       "spi_bus_add_device" in INO and re.search(r"#define\s+TOUCH_CS\s+1\b", INO) is not None)
    ok("touch is read + mapped", "touch_now" in INO and "3807 - rawY" in INO and "3873 - rawX" in INO)
    ok("a fresh press routes to hud_on_touch", "hud_on_touch(" in INO)
    ok("hud_on_touch switches page by tab (top strip)",
       re.search(r"void hud_on_touch\([^)]*\)\s*\{.*?TOP_H.*?hud_mode_set\(", PG, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
