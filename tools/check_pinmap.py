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

# The display pins are READ from TFT_eSPI's User_Setup rather than copied here.
#
# They used to be copied, and that was the bug this table exists to prevent. The
# transcription described the 2.8" ESP32-2432S028R, where the backlight is GPIO
# 21. On the 3.5" ESP32-3248S035R it is GPIO 27 -- so a chip select on 27 would
# fight the backlight, and the copied table had no entry for 27 at all and said
# nothing. That collision was found by eye on real hardware, which is precisely
# the job this script claims in its own docstring.
USER_SETUP = SKETCH.parent / "Libraries" / "User_Setup cyd.h"

TFT_ROLES = {
    "TFT_CS": "TFT CS",     "TFT_DC": "TFT DC",       "TFT_MOSI": "TFT MOSI",
    "TFT_SCLK": "TFT SCK",  "TFT_MISO": "TFT MISO",   "TFT_BL": "TFT backlight",
    "TOUCH_CS": "touch CS",
}


def read_user_setup():
    """Display pins from User_Setup, as the build sees them.

    Run through the same Pre() as the sketch headers rather than scanned line
    by line. It is a short file with no branches in it today, and it is read
    this way because it had them: a line scanner takes whichever arm is
    written first, which is how CC1101's chip select on GPIO 21 once read as
    a collision for one panel and as clear for the other when the truth was
    the reverse.
    """
    out = {}
    if not USER_SETUP.exists():
        return out
    pre = Pre()
    pre.run(USER_SETUP)
    for macro, role in TFT_ROLES.items():
        pin = pre.resolve(macro)
        if isinstance(pin, int) and not isinstance(pin, bool) and pin >= 0:
            out[pin] = role
    return out


# Soldered-down parts of the board that no config file describes, per panel.
#
#   2.8" ESP32-2432S028R   RGB 4/16/17, CdS 34, speaker 26
#   3.5" ESP32-3248S035R   RGB 4/16/17, CdS 34, speaker 26, touch IRQ 36
#
# They agree about more than this table used to claim. Every difference it
# once listed for the 3.5" came from lcdwiki's E32R35T, which is a different
# vendor's board.
#
# The 3.5" row was lcdwiki's E32R35T for most of this port's life: RGB on
# 22/16/17 with GPIO 4 as an audio amplifier's enable. The reference board
# is Sunton's and does not agree with any of that.
#
# Measured on 2026-09-23 by driving each candidate LOW in turn -- the LED is
# common anode, so a pin sinks its own channel -- and watching which colour
# came up:
#
#   GPIO 4   red      so it is an LED channel here, not an amp enable
#   GPIO 16  blue     the old table said green
#   GPIO 17  green    the old table said blue
#   GPIO 22  nothing  not an LED pin on this board at all
#
# Which colour sits on 16 and which on 17 changes nothing -- both are given
# up whole -- but 4 and 22 matter. GPIO 4 being an LED rather than an
# amplifier is the reason this board could spend it; GPIO 22 being ordinary
# free GPIO is what makes CC1101 GDO0 uncontroversial there.
#
# 26, 34 and 36 on the 3.5" are from rzeldent/platformio-espressif32-sunton's
# esp32-3248S035R.json, which is reverse-engineered from Sunton boards rather
# than copied from a vendor page. It agrees with what was metered here -- the
# backlight on 27, the LED on 4/16/17, the SD bus -- which is most of why it
# is trusted for the three that were not.
#
# It also corrects one: GPIO 34 is the CdS photoresistor, the same light
# sensor the 2.8" has, not a battery divider. That claim came from lcdwiki's
# E32R35T along with everything else this board turned out not to be.
#
# Still a published source, and this board has now disagreed with four of
# them. Nothing here depends on 26, 34 or 36.
BOARD_FIXED = {
    35: {4: "RGB LED red", 16: "RGB LED blue", 17: "RGB LED green",
         26: "speaker", 34: "CdS light sensor", 36: "touch IRQ"},
}

def reserved_for():
    """Onboard hardware whose pads are already spoken for."""
    out = dict(BOARD_FIXED[35])
    out.update(read_user_setup())
    return out

# Pins we knowingly repurpose. The RGB LED is the only block of spare GPIO left
# on this board; giving it up is what makes room for three radios. The backlight
# pin is whatever User_Setup says it is, so take it from there rather than
# naming a number that is right on one panel and wrong on the other.
# The backlight is deliberately absent: it is a pin the display needs, on
# whichever panel this build targets, and taking it is the collision that
# started this.
# Per panel, because "spare" is a property of the board and not of the number.
#
# The 2.8" gives up its RGB LED, its speaker and its LDR -- an LED, a buzzer
# The board gives up its RGB LED and nothing else. 26, 34 and 36 stay
# reserved because the board has them and this tool has no use for a speaker,
# a light sensor or the touch controller's interrupt.
#
# GPIO 4 is in the set now that it has been shown to be an LED channel here
# rather than an amplifier's enable. Nothing uses it -- NRF24 CSN is 25 --
# and the reason CSN moved off 4 in the first place was the belief that 4
# keyed an amp. That belief was about a different board, but leaving CSN on
# 25 costs nothing and one fewer pin read off a datasheet is worth more than
# the pin.
REPURPOSABLE = {
    35: {4, 16, 17},
}

# Macros that actually drive a pad.
SIGNALS = {
    "SD_CS": "SD CS", "SD_MOSI": "VSPI MOSI", "SD_MISO": "VSPI MISO",
    "SD_SCLK": "VSPI SCK",
    "CC1101_CS": "CC1101 CS", "CC1101_GDO0": "CC1101 GDO0 (TX)",
    "CC1101_GDO2": "CC1101 GDO2 (RX)",
    "CE_PIN_1": "NRF24 CE", "CSN_PIN_1": "NRF24 CSN",
    "CE_PIN_2": "NRF24 #2 CE", "CSN_PIN_2": "NRF24 #2 CSN",
    "CE_PIN_3": "NRF24 #3 CE", "CSN_PIN_3": "NRF24 #3 CSN",
    "GPS_UART_RX": "GPS RX",
    "MCP23017_SDA": "MCP23017 SDA", "MCP23017_SCL": "MCP23017 SCL",
    "LORA_CS": "LoRa CS", "LORA_RESET": "LoRa RESET", "LORA_BUSY": "LoRa BUSY",
}

# Control lines may live on an MCP23017 expander (owner's board): a pin value
# >= MCP_BASE is channel (pin - MCP_BASE), not a GPIO. Must match Mcp23017::PIN_BASE.
MCP_BASE = 100

# Signals that only exist on some boards; absence is not an error.
OPTIONAL = {"MCP23017_SDA", "MCP23017_SCL", "LORA_CS", "LORA_RESET", "LORA_BUSY"}

# Bus lines are shared on purpose; never report them against each other.
SHARED_BUS = {"SD_MOSI", "SD_MISO", "SD_SCLK",
              "CC1101_SCK", "CC1101_MOSI", "CC1101_MISO"}

# Single-module build: alias -> the _1 macro it is expected to mirror. Sharing
# a pad with that macro is intentional; sharing one with anything else is not.
NRF_ALIASES = {"CE_PIN_2": "CE_PIN_1", "CE_PIN_3": "CE_PIN_1",
               "CSN_PIN_2": "CSN_PIN_1", "CSN_PIN_3": "CSN_PIN_1"}

# Signals allowed to sit on GPIO 34-39, which are input-only on the ESP32.
INPUT_ONLY_OK = {"CC1101_GDO2", "GPS_UART_RX", "SD_MISO", "LORA_BUSY"}

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


def check():
    """Print the map and return the list of errors."""
    reserved = reserved_for()

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
        if pin >= MCP_BASE:
            print(f"  MCP ch{pin - MCP_BASE:>2}  {label:<20}  [on the MCP23017 expander]")
            continue
        if pin < 0:
            print(f"  (none)  {label:<20}  [unassigned]")
            continue
        note = reserved.get(pin, "")
        tag = ""
        if note:
            tag = (f"  [repurposed from {note}]" if pin in REPURPOSABLE[35]
                   else f"  [!! board uses this for {note}]")
        print(f"  GPIO {pin:>2}  {label:<20}{tag}")

    print()
    errors = []
    for macro in unresolved:
        if macro in OPTIONAL:
            continue
        errors.append(f"{macro} did not resolve to an integer")

    # Expander channels used more than once (same alias rule as GPIO below).
    by_ch = {}
    for macro, (pin, _) in pins.items():
        if pin >= MCP_BASE:
            ch = pin - MCP_BASE
            if not (0 <= ch <= 15):
                errors.append("%s is MCP channel %d -- the MCP23017 has 0..15" % (macro, ch))
            by_ch.setdefault(pin, []).append(macro)
    for pin, macros in sorted(by_ch.items()):
        real = [m for m in macros
                if m not in NRF_ALIASES
                or pins.get(NRF_ALIASES[m], (None,))[0] != pin]
        if len(real) >= 2:
            errors.append("MCP ch%d driven by: %s" %
                          (pin - MCP_BASE, ", ".join("%s (%s)" % (m, pins[m][1]) for m in macros)))

    # Two different signals on one GPIO pad (expander channels and unassigned
    # pins -1 are handled/skipped separately).
    by_pin = {}
    for macro, (pin, _) in pins.items():
        if 0 <= pin < MCP_BASE:
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
        if pin in reserved and pin not in REPURPOSABLE[35]:
            errors.append("GPIO %d (%s) collides with onboard %s"
                          % (pin, label, reserved[pin]))

    # Outputs on input-only pads.
    for macro, (pin, label) in sorted(pins.items()):
        if 34 <= pin <= 39 and macro not in INPUT_ONLY_OK:
            errors.append("GPIO %d (%s) is input-only and cannot drive this signal"
                          % (pin, label))

    # The sketch's backlight pin against the display's.
    #
    # Two names for one pad: TFT_BL, which TFT_eSPI drives HIGH at begin(),
    # and BACKLIGHT_PIN, which the sketch attaches a PWM channel to for the
    # Brightness setting. User_Setup has branched per panel for as long as
    # the 3.5" has been supported; shared.h kept the CYD default of 21 for
    # both, and board_pueo.h argued in a comment that 27 was the backlight
    # on the 3.5" without ever saying so in code.
    #
    # Neither half failed loudly. TFT_eSPI lit the real pin, so the screen
    # worked; nothing was soldered to CC1101, so its chip select -- also 21
    # on that build -- never toggled. The only symptom was a Brightness
    # setting that did nothing, on a pad with nothing on the end of it.
    bl = pre.resolve("BACKLIGHT_PIN")
    # reserved is {pin: role}; the display's own backlight is in there under
    # the label TFT_ROLES gives TFT_BL, read from User_Setup for this panel.
    tft_bl = next((p for p, role in reserved.items()
                   if role == TFT_ROLES["TFT_BL"]), None)
    print("  %-8s %-20s %s" % ("", "backlight",
                               "BACKLIGHT_PIN %s, TFT_BL %s" % (bl, tft_bl)))
    if bl is None or tft_bl is None:
        errors.append("could not resolve BACKLIGHT_PIN (%s) or TFT_BL (%s)"
                      % (bl, tft_bl))
    elif bl != tft_bl:
        errors.append(
            "BACKLIGHT_PIN is GPIO %d but the display's backlight is GPIO %d"
            " -- the Brightness setting drives a pad nothing is on" % (bl, tft_bl))

    # The expander base this check assumes must match the firmware's, or a pin
    # flagged here as an expander channel is a real GPIO to the driver (or vice
    # versa) and the whole split is wrong.
    mcp_h = SKETCH / "Mcp23017.h"
    if mcp_h.is_file():
        m = re.search(r"PIN_BASE\s*=\s*(\d+)",
                      mcp_h.read_text(encoding="utf-8", errors="replace"))
        if m and int(m.group(1)) != MCP_BASE:
            errors.append("Mcp23017::PIN_BASE is %s but check_pinmap uses %d"
                          % (m.group(1), MCP_BASE))

    return errors


def main():
    # One panel now. This used to run twice, once per published image, because
    # a pin map that is clean for one and not the other is still a broken
    # release and a single-panel check could not say which.
    errors = check()
    if errors:
        print("COLLISIONS")
        for e in errors:
            print("  x " + e)
        return 1
    print("no collisions")
    return 0


if __name__ == "__main__":
    sys.exit(main())
