#!/usr/bin/env python3
"""Resolve the active board's GPIO assignments and flag collisions.

Runs a cut-down C preprocessor over ESP32-DIV/shared.h: it follows #define,
#ifdef/#ifndef/#if defined()/#elif/#else/#endif and the two board headers, and
throws away everything that is not a directive. That is enough, because every
pin in shared.h is a plain `#define NAME <int>` inside #ifndef guards.

It then cross-checks the result against the pins the CYD board itself already
uses (display, touch, SD, LED), which shared.h does not model. The TFT pins
live in TFT_eSPI's User_Setup, so nothing else in the tree would catch a chip
select landing on, say, the touch clock.

Usage:  python tools/check_pinmap.py
Exit code 1 on any collision.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

# CYD ESP32-2432S028R fixed hardware. Not in shared.h; TFT and touch come from
# TFT_eSPI's User_Setup and the LED/LDR/speaker are simply soldered down.
CYD_RESERVED = {
    15: "TFT CS",        2: "TFT DC",        13: "TFT MOSI",
    14: "TFT SCK",      12: "TFT MISO",      21: "TFT backlight",
    33: "touch CS",     32: "touch MOSI",    39: "touch MISO",
    25: "touch CLK",    36: "touch IRQ",
     4: "RGB LED red",  16: "RGB LED green", 17: "RGB LED blue",
    34: "LDR",          26: "speaker",
}

# Pins we knowingly repurpose. The RGB LED is the only block of spare GPIO left
# on this board; giving it up is what makes room for three radios.
REPURPOSABLE = {4, 16, 17, 21, 26, 34}

# Macros that actually drive a pad.
SIGNALS = {
    "SD_CS": "SD CS", "SD_MOSI": "VSPI MOSI", "SD_MISO": "VSPI MISO",
    "SD_SCLK": "VSPI SCK",
    "CC1101_CS": "CC1101 CS", "CC1101_GDO0": "CC1101 GDO0 (TX)",
    "CC1101_GDO2": "CC1101 GDO2 (RX)",
    "PN532_SS": "PN532 SS",
    "CE_PIN_1": "NRF24 CE", "CSN_PIN_1": "NRF24 CSN",
    "CE_PIN_2": "NRF24 #2 CE", "CSN_PIN_2": "NRF24 #2 CSN",
    "CE_PIN_3": "NRF24 #3 CE", "CSN_PIN_3": "NRF24 #3 CSN",
    "GPS_UART_RX": "GPS RX",
}

# Bus lines are shared on purpose; never report them against each other.
SHARED_BUS = {"SD_MOSI", "SD_MISO", "SD_SCLK",
              "CC1101_SCK", "CC1101_MOSI", "CC1101_MISO",
              "PN532_SCK", "PN532_MOSI", "PN532_MISO"}

# Single-module build: alias -> the _1 macro it is expected to mirror. Sharing
# a pad with that macro is intentional; sharing one with anything else is not.
NRF_ALIASES = {"CE_PIN_2": "CE_PIN_1", "CE_PIN_3": "CE_PIN_1",
               "CSN_PIN_2": "CSN_PIN_1", "CSN_PIN_3": "CSN_PIN_1"}

# Signals allowed to sit on GPIO 34-39, which are input-only on the ESP32.
INPUT_ONLY_OK = {"CC1101_GDO2", "GPS_UART_RX", "SD_MISO"}

DIRECTIVE = re.compile(r"^\s*#\s*(\w+)\s*(.*)$")
DEFINE = re.compile(r"^(\w+)(?:\s+(.*))?$")


class Pre:
    """Just enough preprocessor to resolve pin macros."""

    def __init__(self):
        self.macros = {}

    def truth(self, expr):
        e = re.sub(r"defined\s*\(\s*(\w+)\s*\)",
                   lambda m: "1" if m.group(1) in self.macros else "0", expr)
        e = re.sub(r"defined\s+(\w+)",
                   lambda m: "1" if m.group(1) in self.macros else "0", e)

        def ident(m):
            v = self.macros.get(m.group(0))
            return str(v) if isinstance(v, int) and not isinstance(v, bool) else "0"

        e = re.sub(r"\b[A-Za-z_]\w*\b", ident, e)
        e = e.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
        # Every identifier has now been rewritten to a digit, so what is left is
        # arithmetic and comparisons. Refuse anything else rather than eval it:
        # this only ever sees our own headers, but a stray macro body should
        # fail the check, not run.
        if not re.fullmatch(r"[\d\s()+\-*/%<>=!&|^~]*(?:\b(?:and|or|not)\b[\d\s()+\-*/%<>=!&|^~]*)*", e):
            return False
        try:
            return bool(eval(e, {"__builtins__": {}}, {}))
        except Exception:
            return False

    def run(self, path):
        text = path.read_text(encoding="utf-8", errors="replace")
        # stack frames: [active_now, a_branch_was_taken, parent_was_active]
        stack = []
        for raw in text.splitlines():
            m = DIRECTIVE.match(raw)
            if not m:
                continue
            d, rest = m.group(1), m.group(2).strip()
            live = all(f[0] for f in stack)

            if d in ("ifdef", "ifndef", "if"):
                if d == "ifdef":
                    cond = bool(rest) and rest.split()[0] in self.macros
                elif d == "ifndef":
                    cond = bool(rest) and rest.split()[0] not in self.macros
                else:
                    cond = self.truth(rest)
                stack.append([live and cond, cond, live])
            elif d == "elif" and stack:
                f = stack[-1]
                cond = (not f[1]) and self.truth(rest)
                f[0] = f[2] and cond
                f[1] = f[1] or cond
            elif d == "else" and stack:
                f = stack[-1]
                f[0] = f[2] and not f[1]
                f[1] = True
            elif d == "endif":
                if stack:
                    stack.pop()
            elif d == "define" and live:
                dm = DEFINE.match(rest)
                if dm and "(" not in dm.group(1):
                    self.macros[dm.group(1)] = self._value(dm.group(2) or "")
            elif d == "undef" and live:
                if rest:
                    self.macros.pop(rest.split()[0], None)
            elif d == "include" and live:
                im = re.match(r'"([^"]+)"', rest)
                if im:
                    child = path.parent / im.group(1)
                    if child.exists():
                        self.run(child)

    @staticmethod
    def _value(val):
        val = re.sub(r"/\*.*?\*/", "", val).split("//")[0].strip()
        if val == "":
            return True
        try:
            return int(val, 0)
        except ValueError:
            return val

    def resolve(self, name):
        """Follow macro-to-macro aliases down to an integer."""
        v = self.macros.get(name)
        for _ in range(10):
            if isinstance(v, int) and not isinstance(v, bool):
                return v
            if not isinstance(v, str):
                return None
            if v in self.macros:
                v = self.macros[v]
                continue
            try:
                return int(v, 0)
            except ValueError:
                return None
        return None


def main():
    pre = Pre()
    pre.run(SKETCH / "shared.h")

    print(f"board: {pre.macros.get('ESP32DIV_BOARD_NAME', '?')}")
    print()

    pins = {}
    unresolved = []
    for macro, label in SIGNALS.items():
        pin = pre.resolve(macro)
        if pin is None:
            unresolved.append(macro)
        else:
            pins[macro] = (pin, label)

    for macro, (pin, label) in sorted(pins.items(), key=lambda kv: kv[1][0]):
        note = CYD_RESERVED.get(pin, "")
        tag = ""
        if note:
            tag = (f"  [repurposed from {note}]" if pin in REPURPOSABLE
                   else f"  [!! board uses this for {note}]")
        print(f"  GPIO {pin:>2}  {label:<20}{tag}")

    print()
    errors = []
    for macro in unresolved:
        errors.append(f"{macro} did not resolve to an integer")

    # Two different signals on one pad.
    by_pin = {}
    for macro, (pin, _) in pins.items():
        by_pin.setdefault(pin, []).append(macro)
    for pin, macros in sorted(by_pin.items()):
        if len(macros) < 2:
            continue
        if all(m in SHARED_BUS for m in macros):
            continue
        # An NRF24 alias is only benign when it lands on the same pad as the
        # _1 macro it mirrors. An alias sitting on some unrelated peripheral
        # is a genuine collision, so only drop the ones that match.
        real = [m for m in macros
                if m not in NRF_ALIASES
                or pins.get(NRF_ALIASES[m], (None,))[0] != pin]
        if len(real) < 2:
            continue
        errors.append("GPIO %d driven by: %s" %
                      (pin, ", ".join("%s (%s)" % (m, pins[m][1]) for m in macros)))

    # Landing on onboard hardware we did not consciously give up.
    for macro, (pin, label) in sorted(pins.items()):
        if pin in CYD_RESERVED and pin not in REPURPOSABLE:
            errors.append("GPIO %d (%s) collides with onboard %s"
                          % (pin, label, CYD_RESERVED[pin]))

    # Outputs on input-only pads.
    for macro, (pin, label) in sorted(pins.items()):
        if 34 <= pin <= 39 and macro not in INPUT_ONLY_OK:
            errors.append("GPIO %d (%s) is input-only and cannot drive this signal"
                          % (pin, label))

    if errors:
        print("COLLISIONS")
        for e in errors:
            print("  x " + e)
        return 1

    print("no collisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
