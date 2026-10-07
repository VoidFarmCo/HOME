#!/usr/bin/env python3
"""Feature screens wear the H.O.M.E chrome via one shared helper.

Restyling feature screens to the UI kit, incrementally: each converted screen
draws its header through homeScreenHeader() instead of hand-rolling a title in a
raw colour, so the look is set in one place and matches the home. The helper
itself must use the LIVE accent (accentColor565(settings().accentColor)), not the
fixed UI_ACCENT literal, or converted screens would not follow the user's accent.
CONVERTED grows as screens are migrated. Reads source; needs no board.

    python tools/check_screen_chrome.py
"""
import re
import sys
from pathlib import Path

SK = Path(__file__).resolve().parent.parent / "ESP32-DIV"

# Screens migrated to homeScreenHeader so far (grows per batch).
CONVERTED = ["RadioTest.cpp"]

CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    utils_h = (SK / "utils.h").read_text(encoding="utf-8", errors="replace")
    utils_c = (SK / "utils.cpp").read_text(encoding="utf-8", errors="replace")

    ok("utils.h declares homeScreenHeader", "homeScreenHeader" in utils_h)
    body = re.search(r"void homeScreenHeader\([^)]*\)\s*\{(.*?)\n\}", utils_c, re.S)
    ok("homeScreenHeader is defined", body is not None)
    b = body.group(1) if body else ""
    ok("header uses the LIVE accent (homeAccent())", "homeAccent()" in b)
    ok("header does NOT use the fixed UI_ACCENT literal", "UI_ACCENT" not in b)
    ok("header clears to the themed UI_BG", "UI_BG" in b)
    ok("header draws the accent underline rule",
       re.search(r"fillRect\([^;]*accent\)", b) is not None)

    for fn in CONVERTED:
        src = (SK / fn).read_text(encoding="utf-8", errors="replace")
        ok("%s calls homeScreenHeader" % fn, "homeScreenHeader(" in src)
        ok("%s no longer clears its screen to TFT_BLACK" % fn,
           "fillScreen(TFT_BLACK)" not in src)

    # The shared y=19 header separator is the accent on every feature screen,
    # not the grey UI_LINE -- that is how the scanners read as H.O.M.E.
    import glob
    stray = 0
    for f in glob.glob(str(SK / "*.cpp")):
        stray += len(re.findall(r"drawFastHLine\(0, 19, [^,]+, UI_LINE\)",
                                open(f, encoding="utf-8", errors="replace").read()))
    ok("header separators use the accent, not UI_LINE", stray == 0)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
