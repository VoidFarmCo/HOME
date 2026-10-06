#!/usr/bin/env python3
"""No code pokes an expander-routed control pin with a bare GPIO call.

CC1101 CS and the nRF24 CSN/CE may live on the MCP23017 (owner's board). Every
access must go through Mcp23017::writeAny / pinModeAny, which route to the
expander for a pin >= PIN_BASE and to a GPIO otherwise. A stray
`digitalWrite(CC1101_CS, ...)` or `pinMode(CSN_PIN_1, ...)` compiles and works on
a direct-pin board, then on the owner's board writes to "GPIO 100" -- a no-op --
so the chip-select never toggles and the radio is silently dead.

The driver-specific guards (check_nrf_expander, check_cc1101_expander) cover
Nrf24Raw and the vendored ELECHOUSE lib. This one sweeps the WHOLE sketch
(ESP32-DIV/*.cpp and the .ino), where subghz.cpp and utils.cpp also frame the
CC1101 bus by hand. It reads source; it needs no board.

    python tools/check_expander_cs.py
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

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


BAD = re.compile(r"\b(?:digitalWrite|pinMode)\s*\(\s*(CC1101_CS|CSN_PIN_1|CE_PIN_1)\b")


def main():
    files = sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino"))
    total = 0
    for f in files:
        src = f.read_text(encoding="utf-8", errors="replace")
        # strip comments so a mention in a comment is not a false positive
        s = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        s = re.sub(r"//[^\n]*", "", s)
        hits = BAD.findall(s)
        if hits:
            total += len(hits)
            ok("%s has no raw CS/CE GPIO call" % f.name, False,
               "%d: %s" % (len(hits), ", ".join(sorted(set(hits)))))
        else:
            ok("%s clean" % f.name, True)
    # The helpers must actually be the path used somewhere (sanity).
    anyuse = any("Mcp23017::writeAny" in f.read_text(encoding="utf-8", errors="replace")
                 for f in files)
    ok("the expander-aware helpers are used", anyuse)

    print()
    if FAILED:
        print("FAILED: %d of %d (%d raw calls)" % (len(FAILED), CHECKS, total))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
