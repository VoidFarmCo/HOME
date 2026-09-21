#!/usr/bin/env python3
"""Anchor the status-bar icon rows to the right edge instead of to x=240.

One-shot. Run once, recorded here, refuses to run twice.

Companion to screen_dims_from_panel.py, which fixed the dimensions that were
equal to 240 or 320. These are the ones derived from 240: 31 `iconX` arrays
holding the x position of each 16 px status-bar icon, laid out right to left
from the edge of a 240 px panel.

    {220, 10}                     back arrow, and one icon at the right
    {90, 130, 170, 210, 10}       four on a 40 px pitch, and the back arrow
    {10, 190, 220}                back arrow, and two on a 30 px pitch

On a 320 px panel those sat at 60-70% of the width with a gap to their right
-- which is what "the icons are in the middle of the status bar" looked like
on the first 3.5" photo.

Every value is rewritten as PUEO_SCREEN_W - (240 - v), which is the same
number on a 2.8" build and moves the whole group to the right edge on a
3.5" one. Spacing within a group is preserved because every member shifts by
the same amount.

    220 -> PUEO_SCREEN_W - 20        130 -> PUEO_SCREEN_W - 110
    210 -> PUEO_SCREEN_W - 30         90 -> PUEO_SCREEN_W - 150
    190 -> PUEO_SCREEN_W - 50         50 -> PUEO_SCREEN_W - 190
    170 -> PUEO_SCREEN_W - 70

10 is left alone. It is the back arrow, anchored to the left edge, and it is
the only value in any of these arrays under 50 -- the gap between 10 and 50
is what makes the two groups separable without reading all 31 by eye.

PUEO_SCREEN_W rather than the in-scope SCREEN_WIDTH: most of these sit in a
function that declares one, but ducky.cpp's does not, and a macro from
shared.h is in scope everywhere without having to care which.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
TARGETS = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp", "ducky.cpp"]

DECL = re.compile(r"(static int iconX\[\w+\] = \{)([^}]*)(\})")
LEFT_ANCHORED = 10
PANEL_240 = 240


def rewrite(values_src):
    out = []
    for part in values_src.split(","):
        tok = part.strip()
        if not tok.isdigit():
            return None            # already rewritten, or not a plain list
        v = int(tok)
        if v == LEFT_ANCHORED:
            out.append("10")
        elif 50 <= v <= PANEL_240:
            out.append(f"PUEO_SCREEN_W - {PANEL_240 - v}")
        else:
            return None            # outside the pattern; do not guess
    return ", ".join(out)


def main():
    changed = total = skipped = 0
    for name in TARGETS:
        path = SKETCH / name
        if not path.exists():
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        if "iconX" in src and "PUEO_SCREEN_W -" in src:
            print(f"{name} already anchored to the edge -- this has run before.",
                  file=sys.stderr)
            return 1

        n = [0]

        def repl(m):
            body = rewrite(m.group(2))
            if body is None:
                return m.group(0)
            n[0] += 1
            return m.group(1) + body + m.group(3)

        out = DECL.sub(repl, src)
        found = len(DECL.findall(src))
        if n[0]:
            path.write_text(out, encoding="utf-8", newline="")
            print(f"{name:<16} {n[0]:>3} of {found} icon row(s) re-anchored")
            changed += 1
            total += n[0]
            skipped += found - n[0]

    print(f"\n{total} icon row(s) rewritten across {changed} file(s)")
    if skipped:
        print(f"{skipped} left alone -- values outside the 10 / 50..240 pattern")
    return 0


if __name__ == "__main__":
    sys.exit(main())
