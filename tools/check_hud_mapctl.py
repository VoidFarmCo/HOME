#!/usr/bin/env python3
"""MAP control: zoom, pan, recentre, mark-at-centre, and SEND a mark on COMMS.

The map projects with a zoom step (MAP_MPP) and a pan offset (s_panE/N); a button
row does [-]/[+] zoom, [MARK] drops a point at the view centre, [SEND] broadcasts it
on the keyed net, [ME] recentres; holding pans. Shared marks from teammates draw in
amber. The mark frame is keyed-only (never OPEN) and tagged 0xFE. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
CM = (H / "hud_comms.cpp").read_text(encoding="utf-8", errors="replace")
mt = re.search(r"static void map_touch\(.*?\n\}", PG, re.S); MT = mt.group(0) if mt else ""
mp = re.search(r"static void page_map\(.*?\n\}", PG, re.S); MP = mp.group(0) if mp else ""
sm = re.search(r"void hud_comms_send_mark\(.*?\n\}", CM, re.S); SM = sm.group(0) if sm else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("zoom: a MAP_MPP step table + a zoom index the buttons change",
       "MAP_MPP[]" in PG and "s_mapZoom" in MT and "s_mapZoom++" in MT and "s_mapZoom--" in MT)
    ok("pan: a pan offset the map projects with, moved by edge-pan",
       "s_panE" in MT and "s_panN" in MT and "s_panE += step" in MT
       and "(mE - s_panE) / mpp" in MP)
    ok("holding pans (on_repeat drives map_touch with repeat=true)",
       re.search(r"M_MAP\)\s*\{?\s*map_touch\(x, y, true\)", PG) is not None)
    ok("[ME] recentres (clears the pan)",
       "s_panE = 0; s_panN = 0;" in MT)
    ok("[MARK] drops a point at the VIEW CENTRE (your pos + pan)",
       "g.lat + s_panN / 111320.0" in MT and "g.lon + s_panE / (111320.0 * cosl)" in MT)
    ok("[SEND] broadcasts the mark on COMMS, keyed-net only",
       "hud_comms_send_mark(s_wpLat, s_wpLon)" in MT and "!hud_comms_net_open()" in MT)
    ok("a centre crosshair shows where MARK drops",
       re.search(r"cx - 5, cy, cx \+ 5, cy, HUD_C_WHITE", MP) is not None)
    ok("teammates' shared marks draw on the map (amber)",
       "hud_comms_mark(i, &mk)" in MP and "HUD_C_AMBER" in MP)
    # comms side: the mark frame is keyed-only and tagged 0xFE, routed to its parser
    ok("send_mark is keyed-net only (no plaintext location)",
       "s_netOpen" in SM and "return" in SM and "chat_encrypt(s_netKey" in SM)
    ok("mark frame tagged 0xFE and routed to parse_mark_frame",
       re.search(r"#define\s+MARK_TYPE\s+0xFE", CM) is not None
       and "frame[0] = MARK_TYPE" in SM
       and re.search(r"frame\[0\] == MARK_TYPE\)\s*parse_mark_frame", CM) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
