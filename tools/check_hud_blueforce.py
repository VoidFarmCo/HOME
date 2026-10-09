#!/usr/bin/env python3
"""Blue-force tracking: teammates' GPS over the private net, plotted as friendlies.

A position beacon (tagged 0xFF so chat-only devices drop it) rides the SAME keyed
net crypto as chat and is sent ONLY on a keyed net -- never OPEN, so we don't
announce our location in the clear. Teammates are stored, aged out, and drawn on
RADAR + MAP in green. The loop beacons only on the COMMS page (one radio). Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
CM = (H / "hud_comms.cpp").read_text(encoding="utf-8", errors="replace")
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    sp = re.search(r"static void send_pos\(.*?\n\}", CM, re.S)
    sb = sp.group(0) if sp else ""
    ok("position beacon is sent ONLY on a keyed net (never OPEN)",
       "s_netOpen" in sb and "return" in sb and "chat_encrypt(s_netKey" in sb)
    ok("beacon is tagged POS_TYPE 0xFF (chat-only devices drop it)",
       re.search(r"#define\s+POS_TYPE\s+0xFF", CM) is not None and "frame[0] = POS_TYPE" in sb)
    ok("RX routes a 0xFF frame to the position parser, not the chat parser",
       re.search(r"frame\[0\] == POS_TYPE\)\s*parse_pos_frame", CM) is not None)
    ok("teammate store ages out and ignores our own echo",
       "FRIEND_AGE" in CM and 'strcmp(name, s_name)' in CM and "friend_update(" in CM)
    ok("the loop beacons our position on the comms-radio pages (COMMS + MAP)",
       re.search(r"commsRadio\s*=\s*\(m == M_COMMS \|\| m == M_MAP\)", INO) is not None
       and re.search(r"commsRadio\)\s*\{[^}]*hud_comms_tick\(millis\(\), g\.valid, g\.lat, g\.lon\)", INO) is not None)
    radar = re.search(r"static void page_radar\(.*?\n\}", PG, re.S)
    rb = radar.group(0) if radar else ""
    mapfn = re.search(r"static void page_map\(.*?\n\}", PG, re.S)
    mb = mapfn.group(0) if mapfn else ""
    ok("RADAR plots teammates in green from the GPS fix",
       "hud_comms_friend(i, &fi)" in rb and "geo_dist_brg(g.lat, g.lon, fi.lat, fi.lon" in rb
       and "hud_disc(fx, fy, 3, HUD_C_GREEN)" in rb)
    ok("MAP plots teammates in green",
       "hud_comms_friend(i, &fi)" in mb and "fi.lon - g.lon" in mb
       and "hud_disc(fx, fy, 2, HUD_C_GREEN)" in mb)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
