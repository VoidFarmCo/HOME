#!/usr/bin/env python3
"""Follow-up pass after tools/scope_ui_constants.py.

Two things that conversion left behind.

Preprocessor directives sit at column 0 by convention even inside a function
body, so the swap produced declarations at column 0 in the middle of blocks.
Legal, but it reads like file scope when it is not. Each converted line takes
the indentation of the next real statement in its block.

And two `constexpr int SCREEN_HEIGHT = 320;` are now unused. As macros they
leaked past the end of their function and something downstream may have read
them; as constants nothing does, and the containment check confirmed no use
escaped its block. They are dead, so they go.

One-shot. Read-only if nothing matches.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp"]

NAMES = "SCREEN_WIDTH|SCREEN_HEIGHT|LINE_HEIGHT|MAX_LINES|ICON_NUM|ICON_SIZE" \
        "|STATUS_BAR_Y_OFFSET|STATUS_BAR_HEIGHT|MAX_SSID_LENGTH"

CONVERTED = re.compile(r"^(\s*)constexpr int (%s) = (.+);(\s*//.*)?$" % NAMES)
UNDEF = re.compile(r"^(\s*)#undef (%s)$" % NAMES)

# (file, 1-based line, expected text) -- dead after scoping, verified by
# tools/macro_containment_check.py reporting no escaping uses.
DROP = [
    ("wifi.cpp", 626, "constexpr int SCREEN_HEIGHT = 320;"),
    ("bluetooth.cpp", 4318, "constexpr int SCREEN_HEIGHT = 320;"),
]


def indent_of(s):
    return s[:len(s) - len(s.lstrip())]


def main():
    # Drop the dead ones first, by line, before any reflow shifts numbers.
    problems = []
    loaded = {}
    for fname in FILES:
        raw = (SKETCH / fname).read_bytes()
        nl = "\r\n" if b"\r\n" in raw else "\n"
        loaded[fname] = (raw.decode("utf-8", "surrogateescape").split(nl), nl)

    for fname, ln, want in DROP:
        lines, _ = loaded[fname]
        if ln - 1 >= len(lines) or lines[ln - 1].strip() != want:
            got = lines[ln - 1].strip() if ln - 1 < len(lines) else "<past EOF>"
            problems.append("%s:%d expected %r, found %r" % (fname, ln, want, got))
    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("anchors stale; nothing written", file=sys.stderr)
        return 1

    dropped = 0
    for fname, ln, _ in DROP:
        loaded[fname][0][ln - 1] = None
        dropped += 1

    reindented = 0
    for fname in FILES:
        lines, nl = loaded[fname]
        lines = [l for l in lines if l is not None]

        for i, line in enumerate(lines):
            m = CONVERTED.match(line) or UNDEF.match(line)
            if not m or m.group(1):
                continue  # not ours, or already indented
            # Adopt the indentation of the next ordinary statement below.
            want = None
            for j in range(i + 1, min(i + 40, len(lines))):
                nxt = lines[j]
                if not nxt.strip():
                    continue
                if nxt.lstrip().startswith("#"):
                    continue
                if CONVERTED.match(nxt):
                    continue
                if nxt.strip() in ("}", "};"):
                    break
                want = indent_of(nxt)
                break
            if want:
                lines[i] = want + line.lstrip()
                reindented += 1

        loaded[fname] = (lines, nl)

    for fname, (lines, nl) in loaded.items():
        (SKETCH / fname).write_bytes(nl.join(lines).encode("utf-8", "surrogateescape"))

    print("removed %d dead constants, re-indented %d declarations"
          % (dropped, reindented))
    return 0


if __name__ == "__main__":
    sys.exit(main())
