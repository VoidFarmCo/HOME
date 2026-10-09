#!/usr/bin/env python3
"""RADAR direction-finder for the locked SCAN target.

Bins the target's RSSI by compass heading; the fullest bin is the bearing; bins decay
so a re-sweep moves the arrow. Needs a locked target AND the compass. RADAR draws an
amber arrow to the bearing + a DF readout; a RADAR tap runs compass cal / resets the
sweep. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
DF = (H / "hud_df.cpp").read_text(encoding="utf-8", errors="replace")
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
rad = re.search(r"static void page_radar\(.*?\n\}", PG, re.S); RAD = rad.group(0) if rad else ""
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("active only with a locked target AND the compass",
       "hud_scan_has_target() && hud_compass_present()" in DF)
    ok("bins RSSI by heading, bearing = fullest bin, bins decay",
       "s_bin[DF_BINS]" in DF and "hud_scan_target_rssi()" in DF and "hud_compass_heading()" in DF
       and "s_bin[i]--" in DF and "bin centre" in DF)
    ok("RADAR draws an amber DF arrow + a DF readout",
       "hud_df_bearing()" in RAD and "HUD_C_AMBER" in RAD and '"DF %s' in RAD)
    ok("a RADAR tap runs compass cal / resets the sweep",
       re.search(r"M_RADAR.*hud_compass_start_cal\(\).*hud_df_reset\(\)", PR, re.S) is not None)
    ok("wired into the loop + setup",
       "hud_df_tick(millis())" in INO and "hud_df_begin()" in INO)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
