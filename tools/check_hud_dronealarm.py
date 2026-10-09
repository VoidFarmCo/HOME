#!/usr/bin/env python3
"""Full-screen DRONE alarm: loud takeover with the nearest drone + PILOT bearing.

When a drone is tracked and the alarm is unacknowledged, draw_drone_alarm takes
over the content band (big "DRONE DETECTED", nearest drone range+bearing, and the
PILOT's range+bearing from the operator broadcast). A tap acknowledges it; it
re-arms when the drone list clears. Reads source.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
al = re.search(r"static void draw_drone_alarm\(.*?\n\}", PG, re.S)
A = al.group(0) if al else ""
pd = re.search(r"void hud_page_draw\(.*?\n\}", PG, re.S)
PD = pd.group(0) if pd else ""
pr = re.search(r"void hud_on_press\(.*?\n", PG)
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("alarm headlines a drone detection",
       '"DRONE"' in A and '"DETECTED"' in A)
    ok("alarm shows the nearest drone's range + bearing",
       "geo_dist_brg(g.lat, g.lon, d.lat, d.lon" in A and "best" in A and '"DRONE %dM' in A)
    ok("alarm shows the PILOT's bearing from the operator broadcast",
       "nd.op" in A and "geo_dist_brg(g.lat, g.lon, nd.oplat, nd.oplon" in A and '"PILOT %dM' in A)
    ok("page_draw shows the alarm while a drone is present + unacked, re-arms when clear",
       "hud_engage_drone_count() == 0) s_droneAck = false" in PD and "draw_drone_alarm()" in PD)
    ok("a tap acknowledges the alarm (modal)",
       re.search(r"hud_on_press\([^)]*\)\s*\{\s*//[^\n]*\n\s*if \(hud_engage_drone_count\(\) > 0 && !s_droneAck\) \{ s_droneAck = true; return; \}", PG) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
