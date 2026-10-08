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
    ok("hops the 2.4 GHz channels to hear all nets",
       re.search(r"HOPS\[\]\s*=\s*\{\s*1,\s*6,\s*11", E) is not None and "esp_wifi_set_channel" in E)
    ok("ENGAGE page shows ALERT on a recent attack, else CLEAR + a frame count",
       re.search(r"page_engage\([^)]*\).*?ALERT.*?CLEAR.*?SNIFF", PG, re.S) is not None)
    ok("the loop gives ENGAGE the radio and restores it on exit",
       "hud_engage_enter()" in INO and "hud_engage_leave()" in INO and "hud_engage_tick(" in INO)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
