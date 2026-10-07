#!/usr/bin/env python3
"""Every scan/device list draws its rows big, not at the ~3.5pt default.

The scan lists are the screens you read while scanning. At font 1 size 1 they
were unreadable at arm's length. Each list row renderer now draws the name at
size 2 (16 px) -- the WiFi/Deauth/Probe/Hidden/WPS/ARP two-line rows and the
BLE device name. This pins setTextSize(2) into each renderer so a future edit
cannot quietly shrink a list back. Reads source; needs no board.

    python tools/check_list_text_big.py
"""
import re, sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FAILED = []

def ok(name, cond):
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond: FAILED.append(name)

def func_bodies(src, name):
    """All bodies of a (possibly repeated) function name, brace-matched."""
    out = []
    for m in re.finditer(r"(?:static )?void %s\s*\(" % re.escape(name), src):
        i = src.find("{", m.end())
        if i < 0: continue
        depth = 1; j = i + 1
        while j < len(src) and depth:
            if src[j] == '{': depth += 1
            elif src[j] == '}': depth -= 1
            j += 1
        out.append(src[i:j])
    return out

def main():
    wifi = (SKETCH / "wifi.cpp").read_text(encoding="utf-8", errors="replace")
    ble  = (SKETCH / "bluetooth.cpp").read_text(encoding="utf-8", errors="replace")

    # Each wifi list row renderer must draw at size 2.
    renderers = {
        "drawNetworkRow": 1, "deautherDrawApRow": 1, "probeDrawApRow": 1,
        "drawApRow": 3,        # Hidden, WPS, ARP
        "drawHostRow": 1,      # ARP host list
    }
    for fn, want in renderers.items():
        bodies = func_bodies(wifi, fn)
        ok("%s present (%d)" % (fn, want), len(bodies) >= want)
        ok("%s rows draw at size 2" % fn,
           len(bodies) >= 1 and all("setTextSize(2)" in b for b in bodies))

    # Every WiFi-family list row is tall enough for a big row.
    rowhs = [int(v) for v in re.findall(r"LIST_ROW_H\s*=\s*(\d+)", wifi)]
    ok("all wifi LIST_ROW_H >= 28", bool(rowhs) and all(v >= 28 for v in rowhs))

    # BLE device name drawn big.
    ok("BLE list draws names at size 2",
       re.search(r"s_bleCache\[idx\].*?setTextSize\(2\)", ble, re.S) is not None)
    ok("BLE LIST_ROW_H >= 26",
       any(int(v) >= 26 for v in re.findall(r"LIST_ROW_H\s*=\s*(\d+)", ble)))

    print()
    if FAILED:
        print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0

if __name__ == "__main__":
    sys.exit(main())
