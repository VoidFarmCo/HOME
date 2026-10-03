#!/usr/bin/env python3
"""PUEO_BODY_SIZE is only used where font 1 is selected.

shared.h says so on the constant itself:

    #define PUEO_BODY_FONT 2
    #define PUEO_BODY_SIZE 2    /* for drawString's size argument, font 1 */

Those two 2s mean different things. The font is TFT_eSPI's font 2, which is
proportional and already 16 px tall. The size is a multiplier for font 1, the
5x7 GLCD one, which 2 takes to 12x16. Put the multiplier on the font and the
text is 32 px.

TrackerHunt.cpp did. drawPicker() selects PUEO_BODY_FONT, draws at size 1,
and restored PUEO_BODY_SIZE after each row's right-hand column, so every row
after the first came out 32 px tall in a 40 px row whose two lines are 20
apart. Each label wore the address below it. ApTracker.cpp runs the identical
line safely because it selects font 1, which is where the constant belongs,
and the line was copied from there.

    python tools/check_font_size.py

Reads source; needs no board.

This is not a thing a rendering check could catch. tools/render_screens.py
models the screen rather than executing it, so it drew the picker correctly
while the panel did not, and the two only disagreed on hardware.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "ESP32-DIV"

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


def strip_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def main():
    shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    font = int(re.search(r"#define\s+PUEO_BODY_FONT\s+(\d+)", shared).group(1))
    size = int(re.search(r"#define\s+PUEO_BODY_SIZE\s+(\d+)", shared).group(1))
    print("PUEO_BODY_FONT %d, PUEO_BODY_SIZE %d" % (font, size))
    print("the size is a multiplier for font 1; the font is font %d" % font)
    print()

    # The trap only exists while the two differ. If PUEO_BODY_FONT ever
    # becomes 1 this whole check is about nothing, and it should say so
    # rather than pass quietly.
    ok("the two constants still mean different things", font != 1,
       "PUEO_BODY_FONT is 1 now, so this check has nothing to protect")

    bad = []
    for p in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino")):
        src = strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        # Which font was last selected before each PUEO_BODY_SIZE use.
        marks = []
        for m in re.finditer(r"setTextFont\(\s*([A-Za-z_0-9]+)\s*\)", src):
            marks.append((m.start(), m.group(1)))
        for m in re.finditer(r"setTextSize\(\s*PUEO_BODY_SIZE\s*\)", src):
            before = [f for pos, f in marks if pos < m.start()]
            cur = before[-1] if before else "(none)"
            if cur in ("1",):
                continue
            ln = src[:m.start()].count("\n") + 1
            bad.append((p.name, ln, cur))

    for f, ln, cur in bad:
        print("    %-20s:%-5d setTextSize(PUEO_BODY_SIZE) under font %s"
              % (f, ln, cur))
    ok("every PUEO_BODY_SIZE sits under font 1", not bad,
       "%d do not" % len(bad))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("Font 2 is already 16 px. Multiplying it by the font 1 size")
        print("gives 32, and a 40 px row with its lines 20 apart cannot hold")
        print("two of those. Use setTextSize(1) under PUEO_BODY_FONT.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
