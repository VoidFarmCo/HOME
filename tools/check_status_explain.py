#!/usr/bin/env python3
"""The plain-language status screen exists and reads from the UI kit.

Status::explain(what, why, fix) is H.O.M.E's "what happened / why / what to do"
panel -- it replaces silent or cryptic failures with plain words. Being a H.O.M.E
screen it must colour itself through the kit (homeAccent / UI_BG / UI_TEXT), never
a raw 0xRRRR literal, and it must block until the user dismisses it. Pin the
helper and that the named failure sites call it. Reads source; needs no board.

    python tools/check_status_explain.py
"""
import re, sys
from pathlib import Path

SK = Path(__file__).resolve().parent.parent / "ESP32-DIV"
CHECKS = 0; FAILED = []
def ok(n, c):
    global CHECKS; CHECKS += 1
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)

def main():
    h = SK / "Status.h"; c = SK / "Status.cpp"
    ok("Status.h exists", h.is_file())
    ok("Status.cpp exists", c.is_file())
    if not c.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS)); return 1
    hs = h.read_text(encoding="utf-8", errors="replace")
    cs = c.read_text(encoding="utf-8", errors="replace")

    ok("declares Status::explain(what, why, fix)",
       re.search(r"void\s+explain\(\s*const char\*\s*\w+\s*,\s*const char\*\s*\w+\s*,\s*const char\*", hs) is not None)
    body = re.search(r"void explain\([^)]*\)\s*\{(.*?)\n\}", cs, re.S)
    b = body.group(1) if body else ""
    ok("explain() is defined", bool(b))
    ok("uses the live accent (homeAccent)", "homeAccent()" in b)
    ok("clears to the themed background (UI_BG)", "UI_BG" in b)
    ok("no raw colour literal in the panel",
       re.search(r"0x[0-9A-Fa-f]{4}", b) is None)
    ok("blocks until dismissed (waits on a button/touch)",
       ("isButtonPressed" in b or "featureExitButtonPressed" in b or "readTouchXY" in b))

    # Named failure sites call it (grows as sites are wired).
    wired = 0
    for fn in ("wifi.cpp", "bluetooth.cpp", "ESP32-DIV.ino", "subghz.cpp", "gps.cpp"):
        p = SK / fn
        if p.is_file() and "Status::explain(" in p.read_text(encoding="utf-8", errors="replace"):
            wired += 1
    ok("at least one failure site calls Status::explain", wired >= 1)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS)); return 1
    print("%d checks passed" % CHECKS); return 0

if __name__ == "__main__":
    sys.exit(main())
