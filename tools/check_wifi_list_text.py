#!/usr/bin/env python3
"""The WiFi scanner list draws the SSID big and the details below it.

The scan list is the screen you read while scanning, and at font 1 size 1 it
was ~3.5pt -- unreadable at arm's length. The row is column-aligned monospace,
so it cannot simply double (the fixed columns would leave ~3 chars for the
SSID). Instead each network is a two-line row: the SSID at size 2 (16 px) on
top, the signal / channel / auth at size 1 below it. This pins that layout so a
future edit cannot quietly shrink it back. Reads source; needs no board.

    python tools/check_wifi_list_text.py
"""
import re, sys
from pathlib import Path

CPP = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "wifi.cpp"
FAILED = []

def ok(name, cond):
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond: FAILED.append(name)

def main():
    src = CPP.read_text(encoding="utf-8", errors="replace")
    ok("wifi.cpp exists", bool(src))

    # The WiFi scanner's row renderer.
    m = re.search(r"static void drawNetworkRow\(.*?\n\}", src, re.S)
    body = m.group(0) if m else ""
    ok("drawNetworkRow found", bool(body))
    ok("SSID line drawn at size 2 (big)", "setTextSize(2)" in body)
    ok("detail line drawn at size 1 (small)", "setTextSize(1)" in body)
    # two lines: a second cursor offset below the first (y + N)
    ok("row has a second line below the SSID",
       re.search(r"setCursor\([^,]+,\s*y\s*\+\s*\d+\)", body) is not None)

    # Row height holds both lines (>= 28). Anchor on the Deauther-like block.
    g = re.search(r"Deauther-like list geometry.*?LIST_ROW_H\s*=\s*(\d+)", src, re.S)
    ok("WiFi LIST_ROW_H >= 28 (fits two lines)",
       g is not None and int(g.group(1)) >= 28)

    print()
    if FAILED:
        print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0

if __name__ == "__main__":
    sys.exit(main())
