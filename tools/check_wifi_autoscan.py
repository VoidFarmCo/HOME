#!/usr/bin/env python3
"""The WiFi Scanner re-scans on its own, not only on a Rescan press.

Continuous scan: wifiscanLoop() must re-trigger startWiFiScan() on a millis
timer while the user is watching the list, so results stay live without a
button. It must be gated -- not during a detail view, not mid-scan -- so it does
not fight the user or stack scans. Pin both. Reads source; needs no board.

    python tools/check_wifi_autoscan.py
"""
import re
import sys
from pathlib import Path

WIFI = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "wifi.cpp"
CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    src = WIFI.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"void wifiscanLoop\(\)\s*\{(.*?)\nvoid ", src, re.S)
    if not m:
        m = re.search(r"void wifiscanLoop\(\)\s*\{(.*)", src, re.S)
    body = m.group(1) if m else ""
    ok("wifiscanLoop found", bool(body))

    ok("auto-rescan interval constant defined",
       re.search(r"WIFI_AUTO_RESCAN_MS\s*=", src) is not None)
    # a millis-timer re-trigger of the scan inside the loop
    ok("wifiscanLoop re-triggers startWiFiScan() on a timer",
       re.search(r"millis\(\)\s*-\s*s_lastScanEndMs[^;]*WIFI_AUTO_RESCAN_MS", body, re.S) is not None
       and "startWiFiScanAsync()" in body)
    # gated off during a detail view so it doesn't fight the user
    ok("auto-rescan is gated by !isDetailView",
       re.search(r"!isDetailView[^;{]*WIFI_AUTO_RESCAN_MS|WIFI_AUTO_RESCAN_MS[\s\S]{0,120}startWiFiScan", body) is not None
       and "!isDetailView" in body)

    ok("continuous scan is gated by the s_autoScan toggle",
       "s_autoScan &&" in body or "if (s_autoScan" in body)
    ok("the left button is a Stop/Start toggle",
       's_autoScan ? "Stop" : "Start"' in src)
    ok("BTN_LEFT toggles s_autoScan", "s_autoScan = !s_autoScan" in src)

    ok("has a non-blocking async re-scan kicker",
       "void startWiFiScanAsync()" in src)
    ok("the loop polls scanComplete() to finish the async scan in place",
       "WiFi.scanComplete()" in body and "s_bgScanPending = false" in body)

    # The list is sticky: scans merge by BSSID + age out, not replace, so the
    # count does not flicker while standing still.
    ok("fillWifiCache merges by BSSID (sticky), not replace",
       "memcmp(s_cache[j].bssid" in src and "WIFI_STICKY_GENS" in src)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
