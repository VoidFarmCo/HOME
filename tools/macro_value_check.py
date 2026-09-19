#!/usr/bin/env python3
"""Prove that scoping the UI constants does not change any value.

The risky ones are defined as expressions over other macros:

    #define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)

A macro like that is evaluated at each *use*, against whatever SCREEN_HEIGHT
and LINE_HEIGHT are in effect on that line. Turning it into

    constexpr int MAX_LINES = SCREEN_HEIGHT / LINE_HEIGHT;

evaluates it once, at the definition. Those two agree only if nothing
redefines its operands between the definition and the last use.

For every use of a derived macro this computes both numbers:

  as-is       the operand values in effect on the line of the use
  scoped      the operand values in effect on the line of its definition

Any row where they differ is a value change the refactor would introduce --
or, read the other way, a live bug the macros are currently hiding.

Read-only. Exit 1 if anything differs.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp"]

# Derived macros and the operands they are built from.
DERIVED = {"MAX_LINES": ("SCREEN_HEIGHT", "LINE_HEIGHT")}
SCALARS = ["SCREEN_WIDTH", "SCREEN_HEIGHT", "LINE_HEIGHT", "ICON_NUM",
           "ICON_SIZE", "STATUS_BAR_Y_OFFSET", "STATUS_BAR_HEIGHT",
           "MAX_SSID_LENGTH"]

DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.*?)\s*$")


def strip_strings(line):
    return re.sub(r'"(\\.|[^"\\])*"', '""', line)


def value_at(defs, line):
    """Value of a macro on a given line: the last definition at or above it."""
    best = None
    for ln, val in defs:
        if ln <= line:
            best = val
        else:
            break
    return best


def main():
    bad = 0
    checked = 0

    for fname in FILES:
        p = SKETCH / fname
        if not p.exists():
            continue
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()

        # name -> [(line, raw value)]
        defs = {}
        for i, raw in enumerate(lines, 1):
            m = DEFINE.match(raw)
            if not m:
                continue
            name, val = m.group(1), m.group(2).split("//")[0].strip()
            if name in SCALARS or name in DERIVED:
                defs.setdefault(name, []).append((i, val))

        for derived, operands in DERIVED.items():
            if derived not in defs:
                continue
            dlist = defs[derived]
            pat = re.compile(r"\b%s\b" % derived)

            for k, (dline, dval) in enumerate(dlist):
                # Only expression-valued definitions can drift.
                if not any(op in dval for op in operands):
                    continue
                end = dlist[k + 1][0] if k + 1 < len(dlist) else len(lines) + 1

                # Operand values as seen at the definition.
                at_def = {op: value_at(defs.get(op, []), dline) for op in operands}

                for i in range(dline, min(end, len(lines) + 1)):
                    raw = lines[i - 1]
                    if DEFINE.match(raw):
                        continue
                    if not pat.search(strip_strings(raw)):
                        continue
                    at_use = {op: value_at(defs.get(op, []), i) for op in operands}
                    checked += 1
                    if at_use != at_def:
                        bad += 1
                        print("  DIFFERS  %s:%d  %s" % (fname, i, derived))
                        print("      defined at line %d with %s" % (dline, at_def))
                        print("      used   at line %d with %s" % (i, at_use))

    print()
    print("derived-macro uses checked: %d" % checked)
    if bad:
        print("VALUE CHANGES: %d -- scoping these would alter behaviour" % bad)
        return 1
    print("no value changes: every use resolves the same scoped or not")
    return 0


if __name__ == "__main__":
    sys.exit(main())
