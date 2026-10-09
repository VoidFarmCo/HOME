#!/usr/bin/env python3
"""Drone contact logging: time-stamped rows to /sd/drones.csv.

Logs tracked drones only when the SD is mounted AND a drone is present, rate-limited,
with a real UTC timestamp from the GPS when available (else uptime). Row carries id,
drone + operator lat/lon, rssi, and your own position. The loop ticks it after the
frame flush (shared SPI bus). GPS must parse UTC time/date. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
DL = (H / "hud_dronelog.cpp").read_text(encoding="utf-8", errors="replace")
GP = (H / "hud_gps.cpp").read_text(encoding="utf-8", errors="replace")
GH = (H / "hud_gps.h").read_text(encoding="utf-8", errors="replace")
SD = (H / "hud_sd.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("logs ONLY when the SD is mounted and a drone is present",
       "if (!hud_sd_ok()) return;" in DL and "hud_engage_drone_count()" in DL
       and "if (dn <= 0) return;" in DL)
    ok("logging is rate-limited (not every frame)",
       "LOG_INTERVAL" in DL and re.search(r"now - s_last < LOG_INTERVAL", DL) is not None)
    ok("row carries id, drone + operator lat/lon, rssi, and own position",
       "d.loc ? d.lat" in DL and "d.op ? d.oplat" in DL and "d.rssi" in DL and "g.lat" in DL)
    ok("timestamp uses GPS UTC date+time when available, else uptime",
       "g.date[0] && g.utc[0]" in DL and "millis() / 1000" in DL)
    ok("GPS parses UTC time + date into the fix",
       "char   utc[7]" in GH and "char   date[7]" in GH and "s_fix.utc" in GP and "s_fix.date" in GP)
    ok("SD has an append that writes to /sd/<path>",
       'snprintf(full, sizeof(full), "/sd/%s"' in SD and 'fopen(full, "a")' in SD)
    ok("the loop ticks the logger after the frame flush",
       re.search(r"hud_tick\(millis\(\)\);\s*\n\s*hud_dronelog_tick\(millis\(\)\);", INO) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
