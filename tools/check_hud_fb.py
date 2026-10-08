#!/usr/bin/env python3
"""hud_core owns a PSRAM framebuffer and never draws outside it.

The render core allocs HUD_W*HUD_H*2 bytes in PSRAM and every primitive must
bounds-check against HUD_W/HUD_H -- an off-screen px()/fillRect() corrupts the
heap. Pin the PSRAM alloc path and the bounds guards. Reads source; no board.
"""
import re, sys
from pathlib import Path
C = Path(__file__).resolve().parent.parent / "hud" / "hud_core.cpp"
H = Path(__file__).resolve().parent.parent / "hud" / "hud_core.h"
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    c = C.read_text(encoding="utf-8", errors="replace")
    h = H.read_text(encoding="utf-8", errors="replace")
    ok("framebuffer is RGB565 240x320 (portrait)", "HUD_W 240" in h and "HUD_H 320" in h)
    ok("allocates from PSRAM (MALLOC_CAP_SPIRAM)", "MALLOC_CAP_SPIRAM" in c)
    ok("hud_init returns false on alloc failure",
       re.search(r"if \(!s_fb\) return false;", c) is not None)
    ok("px() bounds-checks both axes",
       re.search(r"\(unsigned\)x < HUD_W && \(unsigned\)y < HUD_H", c) is not None)
    ok("fillRect clips x and y to the buffer",
       "(unsigned)yy >= HUD_H" in c and "(unsigned)xx < HUD_W" in c)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
