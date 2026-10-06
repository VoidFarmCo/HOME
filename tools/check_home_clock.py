#!/usr/bin/env python3
"""The home header shows a live GPS clock, not a frozen or faked time.

The time/date in the home header must come from the GPS accessors (gpsUtcStr /
gpsDateStr), be painted on a full home draw, and be repainted on a timer so it
ticks -- a header drawn once would freeze at boot. This pins all three so a
later edit cannot silently drop the tick or hardcode a time. Reads source.

    python tools/check_home_clock.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INO = ROOT / "ESP32-DIV" / "ESP32-DIV.ino"
GPSH = ROOT / "ESP32-DIV" / "gps.h"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def main():
    src = INO.read_text(encoding="utf-8", errors="replace")
    gpsh = GPSH.read_text(encoding="utf-8", errors="replace")

    ok("gps.h declares gpsUtcStr()", "const char* gpsUtcStr();" in gpsh)
    ok("gps.h declares gpsDateStr()", "const char* gpsDateStr();" in gpsh)

    ok("drawHeaderClock() is defined", "static void drawHeaderClock()" in src)
    # the clock must read the GPS accessors, not a literal
    ok("clock draws gpsUtcStr()", "gpsUtcStr()" in src)
    ok("clock draws gpsDateStr()", "gpsDateStr()" in src)

    # painted on a full home draw
    body = re.search(r"static void drawHomeHeader\(\).*?\n\}", src, re.S)
    ok("drawHomeHeader() paints the clock",
       body is not None and "drawHeaderClock()" in body.group(0))

    # ticks on a timer in the home input loop (only on the tile home, s_group < 0)
    tick = re.search(
        r"static void handlePlaybookHome\(\)\s*\{.*?drawHeaderClock\(\)", src, re.S)
    ok("handlePlaybookHome() re-paints the clock on a timer",
       tick is not None)
    ok("clock tick is gated by a millis() interval",
       re.search(r"millis\(\)\s*-\s*s_clockMs\s*>\s*\d+", src) is not None)
    ok("clock tick only on the tile home (s_group < 0)",
       re.search(r"s_group\s*<\s*0\s*&&\s*millis\(\)\s*-\s*s_clockMs", src) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
