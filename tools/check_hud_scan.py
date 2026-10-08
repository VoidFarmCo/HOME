#!/usr/bin/env python3
"""Live WiFi scan is wired: async (non-blocking), read into the SCAN/RADAR pages.

The single-core C5 renders at ~29 FPS, so the scan MUST be async -- a blocking
WiFi.scanNetworks() would freeze the HUD. hud_scan drives WiFi.scanNetworks(true)
and polls scanComplete(); the sketch brings the radio up and ticks the scanner
every loop; the SCAN and RADAR pages draw the real contact list. Reads source; no board.
"""
import re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
SK = ROOT / "hud"
INO   = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
SCAN  = (SK / "hud_scan.cpp").read_text(encoding="utf-8", errors="replace")
PG    = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("scan starts an ASYNC WiFi scan (non-blocking, won't freeze the HUD)",
       re.search(r"WiFi\.scanNetworks\(\s*true", SCAN) is not None)
    ok("scan polls scanComplete() instead of blocking",
       "scanComplete()" in SCAN and "WIFI_SCAN_RUNNING" in SCAN)
    ok("results are copied out then freed (scanDelete)",
       "WiFi.SSID(" in SCAN and "WiFi.RSSI(" in SCAN and "scanDelete()" in SCAN)
    # BLE is PARKED: the C5 core ships NimBLE (Bluedroid off), so the Bluedroid
    # BLEDevice path crash-loops. hud_scan_begin runs WiFi-only until ported to NimBLE.
    ok("BLE bring-up is disabled (no Bluedroid btStart call) so WiFi works + no crash",
       re.search(r"if \(btStart\(\)\)", SCAN) is None and "BLEDevice::init(" not in SCAN)
    ok("sketch brings the radio up and ticks the scanner every loop",
       "hud_scan_begin()" in INO and "hud_scan_tick(" in INO)
    ok("SCAN page draws the live list, not hardcoded SSIDs",
       "hud_scan_list()" in PG and "hud_scan_count()" in PG
       and 'AP-NETGEAR' not in PG)
    ok("SCAN list scrolls with a cursor + window (scan_move / s_scroll)",
       "scan_move(" in PG and "s_scroll" in PG and "s_sel" in PG)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
