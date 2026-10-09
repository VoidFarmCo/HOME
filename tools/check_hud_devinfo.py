#!/usr/bin/env python3
"""Settings additions: brightness levels + a DEVICE INFO / GPS status screen.

A BRIGHTNESS row cycles the day backlight duty (hud_set_brightness, separate from
stealth). A DEVICE INFO button opens a modal showing build/version, unit, free RAM,
uptime, SD, and live GPS (fix/sats/pos); a tap closes it. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
di = re.search(r"static void draw_info\(.*?\n\}", PG, re.S); DI = di.group(0) if di else ""
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("brightness is a separate day-duty control (not just stealth on/off)",
       "hud_set_brightness" in INO and "s_bright" in INO
       and re.search(r"hud_set_stealth\(bool on\).*s_stealth = on.*s_bright", INO, re.S) is not None)
    ok("a BRIGHTNESS settings row cycles the level",
       "SET_BRT_Y" in PG and "BRIGHTNESS" in PG
       and re.search(r"hud_set_brightness\(c > 200 \? 160", PR) is not None)
    ok("DEVICE INFO modal shows build/version, RAM, uptime, SD",
       "HUD_VERSION" in DI and "getFreeHeap" in DI and "UP " in DI and "hud_sd_ok()" in DI)
    ok("DEVICE INFO modal shows live GPS (fix/sats/position)",
       "hud_gps()" in DI and "g.sats" in DI and "g.valid" in DI and "hud_gps_rxbytes" in DI)
    ok("an INFO settings button opens it; a tap closes it",
       re.search(r"SET_INF_Y.*s_info = true", PR, re.S) is not None
       and "if (s_info) { s_info = false; return; }" in PR)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
