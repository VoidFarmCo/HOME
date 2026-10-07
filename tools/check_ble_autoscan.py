#!/usr/bin/env python3
"""BLE Scan scans continuously in the background, with a Stop/Start toggle.

Mirrors the WiFi scanner: a non-blocking scan (startBLEScanAsync) runs in NimBLE's
task; bleScanLoop snapshots the result into s_bleCache and repaints the list in
place -- no Scanning takeover, no freeze. The list draws from the snapshot, gated
by the s_bleAutoScan Stop/Start toggle and paused in a detail view. Pin it all.
Reads source; needs no board.

    python tools/check_ble_autoscan.py
"""
import re, sys
from pathlib import Path

BT = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "bluetooth.cpp"
CHECKS = 0; FAILED = []
def ok(n, c):
    global CHECKS; CHECKS += 1
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)

def main():
    src = BT.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"void bleScanLoop\(\)\s*\{(.*?)\nvoid ", src, re.S) or re.search(r"void bleScanLoop\(\)\s*\{(.*)", src, re.S)
    body = m.group(1) if m else ""
    ok("bleScanLoop found", bool(body))
    ok("has a non-blocking async scan (startBLEScanAsync)", "void startBLEScanAsync()" in src)
    ok("async scan uses a scan-ended callback", "bleScanEndedCB" in src and "s_bleDone" in src)
    ok("bleScanLoop snapshots results (fillBleCache) and repaints in place",
       "fillBleCache()" in body and "s_bleDone" in body)
    ok("bleScanLoop re-triggers the background scan (continuous)", "startBLEScanAsync()" in body)
    ok("continuous scan is gated by the s_bleAutoScan toggle",
       "s_bleAutoScan &&" in body or "if (s_bleAutoScan" in body)
    ok("auto-rescan is paused in a detail view", "!isDetailView" in body)
    ok("the left button is a Stop/Start toggle", 's_bleAutoScan ? "Stop" : "Start"' in src)
    ok("BTN_LEFT toggles s_bleAutoScan", "s_bleAutoScan = !s_bleAutoScan" in src)
    ok("the list draws from the snapshot cache", "s_bleCache[idx]" in src and "s_bleCacheCount" in src)
    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS)); return 1
    print("%d checks passed" % CHECKS); return 0

if __name__ == "__main__":
    sys.exit(main())
