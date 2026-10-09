#!/usr/bin/env python3
"""MAP mark: drop a point at the view centre, navigate back by range + bearing.

The mark (s_wpSet/s_wpLat/s_wpLon) is dropped by the MARK button at the view centre
(your position + the pan offset), drawn as a cyan diamond via the map projector, and
the readout shows range (m) + bearing to it (geo_dist_brg + cardinal). Sending it on
COMMS is covered by check_hud_mapctl. Reads source.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
mapfn = re.search(r"static void page_map\(.*?\n\}", PG, re.S); MAP = mapfn.group(0) if mapfn else ""
mt = re.search(r"static void map_touch\(.*?\n\}", PG, re.S); MT = mt.group(0) if mt else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("mark state exists (set flag + lat/lon)",
       "s_wpSet" in PG and "s_wpLat" in PG and "s_wpLon" in PG)
    ok("MAP draws the mark as a cyan diamond via the projector",
       "s_wpSet" in MAP and "proj(s_wpLat, s_wpLon" in MAP and "HUD_C_CYAN" in MAP)
    ok("readout shows mark range + bearing (geo_dist_brg + cardinal)",
       re.search(r"geo_dist_brg\(g\.lat, g\.lon, s_wpLat, s_wpLon", MAP) is not None
       and "cardinal(" in MAP and re.search(r'"MARK %dM', MAP) is not None)
    ok("the MARK button drops the mark at the view centre on a valid fix",
       re.search(r"g\.valid.*s_wpLat = g\.lat \+ s_panN.*s_wpSet = true", MT, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
