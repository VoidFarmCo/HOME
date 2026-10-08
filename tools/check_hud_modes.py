#!/usr/bin/env python3
"""The HUD mode machine has five pages and dispatches each one.

Five modes (SCAN/RADAR/MAP/COMMS/ENGAGE); hud_page_draw must route every mode to
its own page function, each mode must have a name, and there must be a way to
switch (auto-cycle for now). A mode with no dispatch case renders nothing but
the chrome. Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
H = (SK / "hud_pages.h").read_text(encoding="utf-8", errors="replace")
C = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    modes = ["M_SCAN", "M_RADAR", "M_MAP", "M_COMMS", "M_ENGAGE"]
    for m in modes:
        ok("enum has %s" % m, m in H)
    ok("M_COUNT terminates the enum", "M_COUNT" in H)
    # every mode dispatches to a page_* function
    pages = {"M_SCAN": "page_scan", "M_RADAR": "page_radar", "M_MAP": "page_map",
             "M_COMMS": "page_comms", "M_ENGAGE": "page_engage"}
    body = re.search(r"void hud_page_draw\(.*?\n\}", C, re.S)
    b = body.group(0) if body else ""
    for m, fn in pages.items():
        ok("%s -> %s()" % (m, fn),
           re.search(r"case\s+%s\s*:\s*%s\(" % (m, fn), b) is not None)
        ok("%s defined" % fn, re.search(r"static void %s\(" % fn, C) is not None)
    ok("every mode is named", all(('"%s"' % m.split("_")[1]) in C for m in modes))
    ok("a switch exists (auto-cycle)", "hud_mode_auto" in C and "% M_COUNT" in C)
    ok("a flashing HOME badge is drawn in the header",
       re.search(r"draw_chrome\([^)]*\)\s*\{.*?hud_text\(3, 6, \"HOME\", 2, trip_now\(\)\)", C, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
