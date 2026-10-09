#!/usr/bin/env python3
"""Field logging, gated by one LOG on/off (the log button).

Writes only when logging is ON and the SD is mounted, to four files: threats.csv
(each new drone by id + deauth bursts), track.csv (GPS breadcrumb on an interval),
comms.csv (each chat msg via the monotonic total), wifi.csv (the TRACKED AP's
seen/lost transitions only). A SETTINGS LOGGING row toggles it; the drone log and
the loop respect it. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
LG = (H / "hud_log.cpp").read_text(encoding="utf-8", errors="replace")
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
DL = (H / "hud_dronelog.cpp").read_text(encoding="utf-8", errors="replace")
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("logging writes ONLY when ON and the SD is mounted",
       re.search(r"if \(!s_enabled \|\| !hud_sd_ok\(\)\) return;", LG) is not None
       and "hud_log_set_enabled" in LG and "hud_log_enabled" in LG)
    ok("four log files: threats / track / comms / wifi",
       '"threats.csv"' in LG and '"track.csv"' in LG and '"comms.csv"' in LG and '"wifi.csv"' in LG)
    ok("threats: new drones deduped by id + deauth events",
       "s_drLogged" in LG and "hud_engage_deauth()" in LG and "DRONE" in LG and "DEAUTH" in LG)
    ok("track: GPS breadcrumb on an interval when valid",
       "TRACK_MS" in LG and "g.valid" in LG and re.search(r"now - s_t < TRACK_MS", LG) is not None)
    ok("comms: logs each message via the monotonic total",
       "hud_comms_total()" in LG and "s_lastTotal" in LG)
    ok("wifi: TRACKED AP only -- seen/lost transitions",
       "hud_scan_has_target()" in LG and "hud_scan_target_seen()" in LG
       and '"SEEN"' in LG and '"LOST"' in LG)
    ok("a SETTINGS LOGGING row toggles it; loop + drone log respect it",
       re.search(r"SET_LOG_Y.*hud_log_set_enabled\(!hud_log_enabled\(\)\)", PR, re.S) is not None
       and "hud_log_tick(millis())" in INO
       and "if (!hud_log_enabled() || !hud_sd_ok()) return;" in DL)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
