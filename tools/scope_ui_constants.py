#!/usr/bin/env python3
"""Turn the per-screen UI macros into scoped constants.

The sketch uses `#define` as a poor man's scoped constant: each feature opens
with its own SCREEN_HEIGHT, ICON_NUM, MAX_LINES and friends. Because macros
have no scope, each block's values leak forward over the rest of the file
until the next block redefines them, and 22 of those redefinitions change the
value. `MAX_LINES` is the sharp one -- defined as an expression, so it
re-evaluates at every use against whatever SCREEN_HEIGHT is in effect there,
not the one it was written against.

Every definition in the sketch already sits inside a namespace or a function
body, so the fix is a straight swap:

    #define ICON_NUM 3          ->   constexpr int ICON_NUM = 3;
    #define MAX_LINES (H / L)   ->   constexpr int MAX_LINES = (H / L);

Same names, so no use site changes and the diff stays merge-friendly. The
derived ones now evaluate once, where they are written.

STATUS_BAR_Y_OFFSET needs an extra step: shared.h defines it as a macro
(default 0), and a live macro would rewrite the declaration itself into
`constexpr int 0 = 20;`. It gets an #undef first. Uses above that point still
see the header's 0, exactly as they do now.

Safety gates, both run before this and both passing:
  tools/macro_value_check.py       no derived value resolves differently
  tools/macro_containment_check.py no use escapes its definition's block

One-shot: re-running refuses, the anchors no longer match.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp"]

NAMES = {"SCREEN_WIDTH", "SCREEN_HEIGHT", "LINE_HEIGHT", "MAX_LINES",
         "ICON_NUM", "ICON_SIZE", "STATUS_BAR_Y_OFFSET", "STATUS_BAR_HEIGHT",
         "MAX_SSID_LENGTH"}

# Defined as a macro in shared.h, so the name must be freed before it can be
# used as a declarator.
NEEDS_UNDEF = {"STATUS_BAR_Y_OFFSET"}

DEFINE = re.compile(r"^(\s*)#\s*define\s+(\w+)\s+(.+?)\s*$")


def main():
    converted = 0
    undefs = 0
    per_file = {}

    for fname in FILES:
        p = SKETCH / fname
        raw = p.read_bytes()
        nl = "\r\n" if b"\r\n" in raw else "\n"
        lines = raw.decode("utf-8", "surrogateescape").split(nl)
        out = []
        n = 0

        for line in lines:
            m = DEFINE.match(line)
            if not m or m.group(2) not in NAMES:
                out.append(line)
                continue

            indent, name, value = m.group(1), m.group(2), m.group(3)

            # Split off a trailing line comment so it survives the rewrite.
            comment = ""
            cpos = value.find("//")
            if cpos >= 0:
                comment = "  " + value[cpos:]
                value = value[:cpos].rstrip()

            if name in NEEDS_UNDEF:
                out.append("%s#undef %s" % (indent, name))
                undefs += 1
            out.append("%sconstexpr int %s = %s;%s" % (indent, name, value, comment))
            converted += 1
            n += 1

        per_file[fname] = (out, nl, n)

    if not converted:
        print("nothing matched; anchors stale, nothing written", file=sys.stderr)
        return 1

    for fname, (out, nl, n) in per_file.items():
        (SKETCH / fname).write_bytes(nl.join(out).encode("utf-8", "surrogateescape"))
        print("  %-16s %d definitions scoped" % (fname, n))

    print()
    print("converted %d macros to constexpr, %d #undef added" % (converted, undefs))
    return 0


if __name__ == "__main__":
    sys.exit(main())
