#!/usr/bin/env python3
"""Check that every use of a contested macro sits inside its definition's scope.

A `#define` inside a function keeps applying after that function ends -- it
runs to the next `#define` of the same name or to end of file. A scoped
constant stops at the closing brace. So the mechanical swap

    #define ICON_NUM 3      ->      constexpr int ICON_NUM = 3;

is only safe when every use the macro governs happens to fall inside the block
it was written in. Where a use escapes that block, the swap would resolve it
to a different definition, or fail to compile.

This walks braces to find each definition's enclosing block, then checks every
use the definition governs against it.

Read-only. Exit 1 if any use escapes.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp"]
NAMES = ["SCREEN_WIDTH", "SCREEN_HEIGHT", "LINE_HEIGHT", "MAX_LINES",
         "ICON_NUM", "ICON_SIZE", "STATUS_BAR_Y_OFFSET", "STATUS_BAR_HEIGHT",
         "MAX_SSID_LENGTH"]

DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.*?)\s*$")


def strip_strings(line):
    """Drop string literals and line comments.

    Comments matter here: subghz.cpp has a comment naming the very macros it
    is avoiding, which a naive scan reports as a use escaping its block.
    """
    line = re.sub(r'"(\\.|[^"\\])*"', '""', line)
    line = re.sub(r"//.*$", "", line)
    return line


def brace_depths(lines):
    """Depth *after* each line, ignoring preprocessor lines and strings."""
    depths = []
    d = 0
    for raw in lines:
        s = raw.lstrip()
        if s.startswith("#"):
            depths.append(d)
            continue
        line = strip_strings(raw)
        line = re.sub(r"//.*$", "", line)
        d += line.count("{") - line.count("}")
        depths.append(d)
    return depths


def enclosing_block(depths, dline):
    """(start, end) of the innermost block containing 1-based line `dline`."""
    d = depths[dline - 1]
    if d <= 0:
        return (1, len(depths))  # file scope
    start = dline
    while start > 1 and depths[start - 2] >= d:
        start -= 1
    end = dline
    while end < len(depths) and depths[end - 1] >= d:
        end += 1
    return (start, end)


def main():
    escapes = 0
    checked = 0
    for fname in FILES:
        p = SKETCH / fname
        if not p.exists():
            continue
        lines = p.read_text(encoding="utf-8", errors="replace").splitlines()
        depths = brace_depths(lines)

        defs = {}
        for i, raw in enumerate(lines, 1):
            m = DEFINE.match(raw)
            if m and m.group(1) in NAMES:
                defs.setdefault(m.group(1), []).append(i)

        for name, dlines in defs.items():
            pat = re.compile(r"\b%s\b" % name)
            for k, dline in enumerate(dlines):
                govern_end = dlines[k + 1] - 1 if k + 1 < len(dlines) else len(lines)
                bstart, bend = enclosing_block(depths, dline)
                for i in range(dline + 1, govern_end + 1):
                    raw = lines[i - 1]
                    if DEFINE.match(raw):
                        continue
                    if not pat.search(strip_strings(raw)):
                        continue
                    checked += 1
                    if not (bstart <= i <= bend):
                        escapes += 1
                        print("  ESCAPES  %s:%d uses %s defined at %d "
                              "(block %d..%d)" % (fname, i, name, dline, bstart, bend))

    print()
    print("governed uses checked: %d" % checked)
    if escapes:
        print("USES OUTSIDE DEFINING BLOCK: %d -- not safe to scope blindly" % escapes)
        return 1
    print("every use falls inside its definition's block")
    return 0


if __name__ == "__main__":
    sys.exit(main())
