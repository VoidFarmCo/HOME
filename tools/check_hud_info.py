#!/usr/bin/env python3
"""Dual-band is visible + switchable, and there's a Marauder/Bruce-style info view.

Each SCAN row is band-tagged (2G/5G, coloured); the SETTINGS panel has a BAND
filter that cycles ALL/2.4/5 so you can switch between bands; and centre-ENTER on a
row opens a network info view showing band, channel, RSSI, encryption and BSSID.
Reads source; no board.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("each SCAN row shows its band (2G/5G) from the channel",
       re.search(r"page_scan\(.*?hud_band_of\(c\[i\]\.ch\).*?\"5G\".*?\"2G\"", PG, re.S) is not None)
    ok("a band filter hides the other band (vis_count / vis_index / band_ok)",
       "band_ok(" in PG and "vis_count()" in PG and "vis_index(" in PG)
    ok("SETTINGS has a BAND toggle that switches the scan band (2.4 <-> 5)",
       "BAND:" in PG and re.search(r"s_band = \(s_band == 1\) \? 2 : 1", PG) is not None
       and "hud_scan_set_band(" in PG)
    ok("centre-ENTER on a SCAN row opens the info view",
       re.search(r"else \{\s*//[^\n]*ENTER.*?s_detail = true", PG, re.S) is not None)
    ok("info view shows band + channel + RSSI + encryption + BSSID",
       "void draw_detail()" in PG and "BAND" in PG and "CHAN" in PG
       and "RSSI" in PG and "hud_enc_name(" in PG and "BSSID" in PG)
    ok("info view is modal (drawn instead of the page, closes on tap)",
       re.search(r"if \(s_detail\)\s*\{?\s*draw_detail\(\)", PG) is not None
       and re.search(r"if \(s_detail\) \{ s_detail = false;", PG) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
