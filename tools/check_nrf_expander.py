#!/usr/bin/env python3
"""The nRF24 drives its CSN/CE through the expander-aware helpers, never bare GPIO.

On the owner's board the nRF24's CSN and CE live on the MCP23017 (the board has
too few direct output pins -- docs/home/board.md). So Nrf24Raw must toggle those
two lines through `Mcp23017::writeAny` / `pinModeAny`, which route to the expander
when the pin is an expander pin and to a plain GPIO otherwise. A regression to a
bare `digitalWrite(CSN_PIN_1, ...)` would compile and work on Pueo's direct-pin
board, then silently do nothing on the owner's board -- the chip-select would
never toggle and the nRF would never answer, with nothing on screen to say why.

This holds that every CSN/CE access in Nrf24Raw goes through the helpers. It reads
source; it needs no board.

    python tools/check_nrf_expander.py
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


def main():
    src = (SKETCH / "Nrf24Raw.cpp").read_text(encoding="utf-8", errors="replace")

    ok('Nrf24Raw.cpp includes "Mcp23017.h"', '#include "Mcp23017.h"' in src)

    print("\nCSN/CE go through the expander-aware helpers:")
    ok("CSN/CE are driven with Mcp23017::writeAny",
       "Mcp23017::writeAny" in src)
    ok("CSN/CE direction is set with Mcp23017::pinModeAny",
       "Mcp23017::pinModeAny" in src)

    print("\nno bare GPIO call on the nRF control pins:")
    for bad in (r"\bdigitalWrite\s*\(\s*CSN_PIN_1",
                r"\bdigitalWrite\s*\(\s*CE_PIN_1",
                r"\bpinMode\s*\(\s*CSN_PIN_1",
                r"\bpinMode\s*\(\s*CE_PIN_1"):
        hit = re.search(bad, src)
        label = bad.replace(r"\b", "").replace(r"\s*\(\s*", "(")
        ok("no `%s...`" % label, hit is None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
