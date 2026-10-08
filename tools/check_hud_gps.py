#!/usr/bin/env python3
"""NEO-7M GPS parser + MAP moving-map base.

Parses NMEA $..GGA (fix/sats/alt) and $..RMC (position/course); ddmm.mmmm converts
to decimal degrees via deg + min/60; UART opens at 9600 on RX4/TX5. The MAP page
reads the live fix and shows ACQUIRING until there is one. Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
G  = (SK / "hud_gps.cpp").read_text(encoding="utf-8", errors="replace")
PG = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("ddmm.mmmm converts to decimal degrees (min / 60)",
       "nmea_deg" in G and re.search(r"min\s*/\s*60", G) is not None)
    ok("negates S / W hemispheres",
       re.search(r"hemi\s*==\s*'S'\s*\|\|\s*hemi\s*==\s*'W'", G) is not None)
    ok("parses GGA (fix quality + sats) and RMC (A status)",
       'strstr(f[0], "GGA")' in G and 'strstr(f[0], "RMC")' in G and 'f[2][0] == \'A\'' in G)
    ok("UART opens at 9600, RX=IO4, TX=IO28 (unwired; keeps IO8/IO9 for I2C)",
       re.search(r"Serial1\.begin\(9600,\s*SERIAL_8N1,\s*4,\s*28\)", G) is not None)
    ok("sketch ticks the GPS each loop",
       "hud_gps_begin()" in INO and "hud_gps_tick()" in INO
       and "hud_gps_inject" not in INO)   # the self-test inject was removed
    ok("MAP page reads the live fix and shows ACQUIRING until a fix",
       re.search(r"page_map\([^)]*\).*?hud_gps\(\).*?ACQUIRING", PG, re.S) is not None)
    ok("GPS parses speed (knots) from RMC",
       re.search(r"s_fix\.knots = atof\(f\[7\]\)", G) is not None)
    ok("MAP shows a breadcrumb trail + heading + speed",
       "s_trLat" in PG and "cardinal(" in PG and "MPH" in PG)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
