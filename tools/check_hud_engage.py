#!/usr/bin/env python3
"""ENGAGE: passive WiFi deauth/attack detector.

In promiscuous mode, hopping 2.4 GHz channels, it counts 802.11 deauth(0x0C)/
disassoc(0x0A) mgmt frames; the ENGAGE page shows ALERT when they're recent, else
CLEAR, plus a sniffer-alive frame count. RX only (no transmit). The loop gives it
the radio on the ENGAGE page (scanner paused) and restores it on exit. Reads source.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
E   = (SK / "hud_engage.cpp").read_text(encoding="utf-8", errors="replace")
PG  = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("promiscuous RX with a MGMT filter (RX only, no transmit)",
       "esp_wifi_set_promiscuous(true)" in E and "WIFI_PROMIS_FILTER_MASK_MGMT" in E
       and "esp_wifi_set_promiscuous_rx_cb" in E and "esp_wifi_80211_tx" not in E)
    ok("counts deauth (0x0C) + disassoc (0x0A) frames",
       "0x0C" in E and "0x0A" in E and "s_deauth" in E)
    ok("hops BOTH bands (2.4 + 5 GHz) switching band per lap",
       "{2, 1}" in E and "{5, 36}" in E and "esp_wifi_set_band_mode" in E and "apply_hop(" in E)
    ok("ENGAGE page shows the WiFi attack status + a sniffer frame count",
       re.search(r"page_engage\([^)]*\).*?WIFI.*?SNIFF", PG, re.S) is not None)
    ok("the loop gives ENGAGE the radio and restores it on exit",
       "hud_engage_enter()" in INO and "hud_engage_leave()" in INO and "hud_engage_tick(" in INO)
    # --- Remote-ID drone detection ---
    ok("decodes Open Drone ID from beacons (OUI FA-0B-BC, vendor type 0x0D)",
       "0xFA" in E and "0x0B" in E and "0xBC" in E and "0x0D" in E and "parse_odid(" in E)
    ok("parses drone location + operator location (int32 x1e-7)",
       "M + 5" in E and "M + 9" in E and "M + 2" in E and "M + 6" in E and "1e-7" in E)
    ok("ENGAGE shows drones with range + bearing from the GPS fix",
       "hud_engage_drone_count()" in PG and "geo_dist_brg(" in PG and "DRN %dM" in PG)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
