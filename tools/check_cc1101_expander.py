#!/usr/bin/env python3
"""The CC1101 drives its chip-select through the expander-aware helper.

On the owner's board the CC1101's CS is on the MCP23017 (docs/home/board.md). The
vendored ELECHOUSE library drives its SS_PIN with bare `digitalWrite(SS_PIN, ...)`
in ~18 places; every one is routed through `Mcp23017::writeAny` / `pinModeAny` so
SS_PIN can be an expander pin or a GPIO with the same code. A single bare
`digitalWrite(SS_PIN, ...)` left behind would work on Pueo's direct-pin board and
then silently never assert CS on the owner's board -- the radio would init to
nothing, with no error.

This holds that NO bare SS_PIN GPIO call survives in the library. It reads the
vendored source under libs/; it needs no board.

    python tools/check_cc1101_expander.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
LIB = REPO / "libs" / "SmartRC-CC1101-Driver-Lib" / "ELECHOUSE_CC1101_SRC_DRV.cpp"

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
    ok("vendored ELECHOUSE .cpp exists", LIB.is_file())
    if not LIB.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    src = LIB.read_text(encoding="utf-8", errors="replace")

    print("CS routed through the link-time expander wrappers:")
    # The lib is compiled apart from the sketch, so it can't include Mcp23017.h;
    # it declares homeExpWrite/homeExpMode extern and calls them, and the sketch
    # (Mcp23017.cpp) defines them forwarding to Mcp23017::writeAny / pinModeAny.
    ok("lib declares the extern wrappers", "homeExpWrite" in src and "homeExpMode" in src)
    mcp = (REPO / "ESP32-DIV" / "Mcp23017.cpp").read_text(encoding="utf-8", errors="replace")
    ok("Mcp23017.cpp defines homeExpWrite -> writeAny",
       re.search(r"homeExpWrite\s*\([^)]*\)\s*\{[^}]*writeAny", mcp) is not None)
    ok("Mcp23017.cpp defines homeExpMode -> pinModeAny",
       re.search(r"homeExpMode\s*\([^)]*\)\s*\{[^}]*pinModeAny", mcp) is not None)

    print("\nno bare GPIO call on SS_PIN survives:")
    dw = re.findall(r"\bdigitalWrite\s*\(\s*SS_PIN\b", src)
    pm = re.findall(r"\bpinMode\s*\(\s*SS_PIN\b", src)
    ok("no bare digitalWrite(SS_PIN, ...)", not dw, "%d left" % len(dw))
    ok("no bare pinMode(SS_PIN, ...)", not pm, "%d left" % len(pm))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
