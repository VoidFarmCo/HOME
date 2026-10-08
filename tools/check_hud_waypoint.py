#!/usr/bin/env python3
"""MAP waypoint: drop a mark at your GPS, navigate back by range + bearing.

A single waypoint (s_wpSet/s_wpLat/s_wpLon) you drop at your current position on
the MAP page; a tap toggles it (drop when valid-fix / clear). page_map draws it as
a marker and the readout shows range (m) + bearing (geo_dist_brg + cardinal).
Reads source.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
mapfn = re.search(r"static void page_map\(.*?\n\}", PG, re.S)
MAP = mapfn.group(0) if mapfn else ""
press = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S)
PRESS = press.group(0) if press else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("waypoint state exists (set flag + lat/lon)",
       "s_wpSet" in PG and "s_wpLat" in PG and "s_wpLon" in PG)
    ok("MAP draws the waypoint marker relative to you",
       "s_wpSet" in MAP and "s_wpLon - g.lon" in MAP and "HUD_C_CYAN" in MAP)
    ok("readout shows waypoint range + bearing (geo_dist_brg + cardinal)",
       re.search(r"geo_dist_brg\(g\.lat, g\.lon, s_wpLat, s_wpLon", MAP) is not None
       and "cardinal(" in MAP and re.search(r'"WP %dM', MAP) is not None)
    ok("a MAP tap toggles the waypoint, dropping only on a valid fix",
       "M_MAP" in PRESS and "s_wpSet = false" in PRESS
       and re.search(r"g\.valid.*s_wpLat = g\.lat.*s_wpSet = true", PRESS, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
