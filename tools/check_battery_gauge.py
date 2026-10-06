#!/usr/bin/env python3
"""A board that cannot read its battery must not draw a battery gauge.

The status bar paints a battery outline and an "N%" readout from
readBatteryVoltage(), which reads BATTERY_ADC_PIN. On this board -- the Sunton
ESP32-3248S035R, the BOARD_CYD path -- there is no divider from the cell to any
free ADC pin (GPIO 34 is the CdS light sensor, 35 is CC1101 RX, 36/39 are the
touch controller; the BAT1 cell goes straight into the FM5324GA charger). So
BATTERY_ADC_PIN is -1, readBatteryVoltage() reads a dead pin, and the gauge sat
pinned at a fake 0% with an empty red battery -- a readout that looks like a
flat pack on a board that simply has no gauge. See docs/pueo/hardware.md.

The fix is to draw the gauge only when a real sense pin exists. This check holds
that the battery outline and the "%" readout in drawStatusBar are both inside a
compile-time guard keyed on BATTERY_ADC_PIN >= 0, so the fake gauge cannot come
back, and so a board that DOES wire a sense pin (BATTERY_ADC_PIN set by its
overlay) gets the gauge back for free without touching this code.

It reads source; it does not need a board.

    python tools/check_battery_gauge.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

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


def function_body(src, name):
    """`void name(...) {` to its matching close brace. Same shape as
    check_status_bar.py so the two agree on what a function is."""
    m = re.search(r"^(?:static\s+)?void\s+" + re.escape(name) +
                  r"\s*\([^;{]*\)\s*\{", src, re.M)
    if not m:
        return None
    depth, i = 0, m.end() - 1
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[m.end():i]
        i += 1
    return None


# A line is "battery-guarded" when some #if currently open above it has a
# condition that admits only a real sense pin: BATTERY_ADC_PIN >= 0. A plain
# `#if BATTERY_ADC_PIN` does NOT qualify -- -1 is truthy, so it would still
# draw the fake gauge -- which is why the comparison, not just the name, is
# what this looks for.
GUARD_RE = re.compile(r"BATTERY_ADC_PIN\s*>=\s*0")


def guarded_lines(body):
    """Return the set of line numbers (0-based, within `body`) that sit inside
    an open BATTERY_ADC_PIN >= 0 guard. Tracks nesting with a stack so a guard
    wrapped in another #if still counts."""
    guarded = set()
    stack = []  # each entry: True if this #if level is a battery guard
    for n, line in enumerate(body.splitlines()):
        s = line.strip()
        if re.match(r"#\s*if", s):
            stack.append(bool(GUARD_RE.search(s)))
        elif re.match(r"#\s*elif", s):
            if stack:
                stack[-1] = bool(GUARD_RE.search(s))
        elif re.match(r"#\s*(endif|else)", s):
            if re.match(r"#\s*endif", s) and stack:
                stack.pop()
            elif re.match(r"#\s*else", s) and stack:
                stack[-1] = False
        if any(stack):
            guarded.add(n)
    return guarded


def line_of(body, pattern):
    """0-based line number of the first line matching `pattern`, or None."""
    rx = re.compile(pattern)
    for n, line in enumerate(body.splitlines()):
        if rx.search(line):
            return n
    return None


def main():
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")

    body = function_body(utils, "drawStatusBar")
    ok("drawStatusBar found in utils.cpp", body is not None)
    if body is None:
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1

    guarded = guarded_lines(body)

    print("\nthe gauge is behind a sense-pin guard:")
    # The battery outline: drawRoundRect(x, y, 22, 10, ...). The nub beside it
    # and the fill are drawn right after it off the same x/y, so the outline is
    # the anchor the whole glyph follows.
    outline = line_of(body, r"drawRoundRect\(\s*x\s*,\s*y\s*,\s*22\s*,\s*10")
    ok("the battery outline is drawn in drawStatusBar", outline is not None,
       "drawRoundRect(x, y, 22, 10, ...) not found")
    if outline is not None:
        ok("the battery outline is behind BATTERY_ADC_PIN >= 0",
           outline in guarded,
           "line %d draws the battery glyph unconditionally" % (outline + 1))

    # The readout: print(String(batteryPercentage) + "%").
    pct = line_of(body, r'batteryPercentage\s*\)\s*\+\s*"%"')
    ok("the percentage readout is printed in drawStatusBar", pct is not None,
       'print(String(batteryPercentage) + "%") not found')
    if pct is not None:
        ok("the percentage readout is behind BATTERY_ADC_PIN >= 0",
           pct in guarded,
           "line %d prints the %% readout unconditionally" % (pct + 1))

    # The guard must actually be present as source, keyed to the comparison and
    # not just the macro name (a bare `#if BATTERY_ADC_PIN` admits -1).
    ok("a BATTERY_ADC_PIN >= 0 guard exists in drawStatusBar",
       any(GUARD_RE.search(l) and re.match(r"#\s*if", l.strip())
           for l in body.splitlines()),
       "no `#if BATTERY_ADC_PIN >= 0` in the function")

    print("\nthe guard matters for this board:")
    # If the CYD default stopped being -1, the guard would be dead code and the
    # whole reason for it gone; this keeps the check honest about WHY it exists.
    cyd = re.search(r"defined\(BOARD_CYD\)\s*\n\s*#define\s+BATTERY_ADC_PIN\s+(-?\d+)",
                    shared)
    ok("shared.h still disables the gauge on BOARD_CYD (pin -1)",
       cyd is not None and int(cyd.group(1)) < 0,
       "BOARD_CYD BATTERY_ADC_PIN is %s" % (cyd.group(1) if cyd else "not found"))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
