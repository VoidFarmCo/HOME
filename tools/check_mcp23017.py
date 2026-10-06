#!/usr/bin/env python3
"""The MCP23017 driver addresses the right registers.

The MCP23017 is a 16-bit I2C GPIO expander. On the owner's board (see
docs/home/board.md) it carries the radio chip-selects so CC1101 + nRF24 + LoRa
fit -- the board has only three direct output pins. A register driver that reads
or writes the WRONG register is the classic silent bug: it compiles, the I2C ACKs,
and a chip-select never toggles, so a radio simply never answers. Nothing on the
device would say why.

So this pins the register map to the datasheet (IOCON.BANK=0, the power-on default)
and checks the driver drives OUTPUTS through OLAT, reads INPUTS from GPIO, sets
direction in IODIR, and proves it is present with an I2C ACK (endTransmission).
It reads source; it needs no board (the chip is not even wired yet).

    python tools/check_mcp23017.py
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


# Datasheet register addresses, IOCON.BANK = 0 (reset default).
REGS = {
    "IODIRA": 0x00, "IODIRB": 0x01,
    "GPPUA": 0x0C, "GPPUB": 0x0D,
    "GPIOA": 0x12, "GPIOB": 0x13,
    "OLATA": 0x14, "OLATB": 0x15,
}


def const_value(src, name):
    """Value of a `... NAME = 0xNN;` constant (constexpr/#define/enum), or None."""
    m = re.search(name + r"\s*=\s*(0x[0-9A-Fa-f]+|\d+)", src)
    if not m:
        m = re.search(r"#define\s+" + name + r"\s+(0x[0-9A-Fa-f]+|\d+)", src)
    if not m:
        return None
    return int(m.group(1), 0)


def main():
    h = SKETCH / "Mcp23017.h"
    c = SKETCH / "Mcp23017.cpp"
    ok("Mcp23017.h exists", h.is_file())
    ok("Mcp23017.cpp exists", c.is_file())
    if not (h.is_file() and c.is_file()):
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    hsrc = h.read_text(encoding="utf-8", errors="replace")
    csrc = c.read_text(encoding="utf-8", errors="replace")
    src = hsrc + "\n" + csrc

    print("register map matches the datasheet (BANK=0):")
    for name, val in REGS.items():
        got = const_value(src, name)
        ok("%s == 0x%02X" % (name, val), got == val,
           "got %s" % (("0x%02X" % got) if got is not None else "MISSING"))

    print("\nthe driver uses the right register for each direction:")
    # Body of a `<ret> NAME(...) {` definition in the .cpp (brace-matched).
    def body(name):
        m = re.search(r"\b(?:void|int|bool|uint8_t)\s+" + name + r"\s*\([^;{]*\)\s*\{", csrc)
        if not m:
            return ""
        depth, i = 0, m.end() - 1
        while i < len(csrc):
            if csrc[i] == "{":
                depth += 1
            elif csrc[i] == "}":
                depth -= 1
                if depth == 0:
                    return csrc[m.end():i]
            i += 1
        return ""
    dw, dr, pm = body("digitalWrite"), body("digitalRead"), body("pinMode")
    # Outputs are driven through OLAT, inputs read from GPIO, direction set in IODIR.
    ok("digitalWrite drives OLAT (not GPIO directly)",
       "OLAT" in dw and "GPIOA" not in dw and "GPIOB" not in dw)
    ok("digitalRead reads GPIO", "GPIO" in dr)
    ok("pinMode sets IODIR", "IODIR" in pm)

    print("\npin split and presence:")
    # Port B is pins 8..15; the bit within a port is pin & 7.
    ok("splits ports at pin 8 (>= 8 -> port B)",
       re.search(r"(pin|p)\s*(>=|>)\s*(8|7)", src) is not None)
    ok("masks the bit with & 7 (or & 0x07)",
       re.search(r"&\s*(7|0x0?7)\b", src) is not None)
    # present() proves the chip ACKs on the bus.
    ok("present() uses an I2C ACK (endTransmission)",
       re.search(r"present\b.*?endTransmission", src, re.S) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
