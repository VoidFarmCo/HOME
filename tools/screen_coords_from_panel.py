#!/usr/bin/env python3
"""Rewrite 240x320 coordinates inside drawing calls to follow the panel.

One-shot. Run once, recorded here, refuses to run twice.

Third and last of the panel sweep. screen_dims_from_panel.py fixed the
declarations (`#define SCREENHEIGHT 320`), icon_rows_from_width.py fixed the
status-bar rows, and this fixes the literals sitting in the argument lists of
tft.* calls -- 161 of them, which is why the packet monitor's waterfall and
the WiFi scanner's rows still stopped two thirds of the way across a 3.5"
panel after the first two ran.

Position matters, so this does not swap numbers by value. Each TFT_eSPI call
it touches has a known signature, and a literal is only rewritten when it
sits in a slot that means a horizontal or a vertical measurement:

    fillRect(x, y, w, h, colour)          x,w -> horizontal   y,h -> vertical
    drawLine(x0, y0, x1, y1, colour)      x0,x1 -> horizontal y0,y1 -> vertical
    drawFastHLine(x, y, w, colour)        w -> horizontal
    drawFastVLine(x, y, h, colour)        h -> vertical

and then only for values that are unambiguously derived from 240x320:

    horizontal   240 -> PUEO_SCREEN_W        120 -> PUEO_SCREEN_W / 2
                 230 -> PUEO_SCREEN_W - 10   220 -> PUEO_SCREEN_W - 20
                 200 -> PUEO_SCREEN_W - 40
    vertical     320 -> PUEO_SCREEN_H        160 -> PUEO_SCREEN_H / 2
                 304 -> PUEO_SCREEN_H - 16   300 -> PUEO_SCREEN_H - 20

Everything else is left alone. A 16 is a row height on either panel; a 91 is
someone's chosen offset; and this cannot tell a coordinate that happens to be
200 from one that means "40 in from the right". The values above are the ones
where 240x320 is the only reading that makes sense, and each rewrite is
printed so the diff can be read rather than trusted.

A call whose arguments contain a nested call with a comma in it is skipped
rather than mis-split, and reported at the end.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
# Run in two passes. The first named the four big UI files and missed the
# per-feature ones, which is the same mistake this whole sweep exists to fix
# -- a list of places, maintained by hand, that was not the whole list. The
# guard below skips a file already done rather than refusing the run, so the
# second pass could pick up the stragglers.
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp", "ducky.cpp",
         "menu.cpp", "KeyboardUI.cpp",
         "Spotter.cpp", "FastPairScan.cpp", "TrackerFollow.cpp", "rfid.cpp",
         "gps.cpp", "Eapol.cpp", "FastPair.cpp", "FastPairProbe.cpp",
         "SubFile.cpp", "Nrf24Raw.cpp", "Touchscreen.cpp"]

H, V = "h", "v"

# Argument slots, by TFT_eSPI signature. None = leave that slot alone.
SIGS = {
    "fillRect":        [H, V, H, V, None],
    "drawRect":        [H, V, H, V, None],
    "fillRoundRect":   [H, V, H, V, None, None],
    "drawRoundRect":   [H, V, H, V, None, None],
    "fillSmoothRoundRect": [H, V, H, V, None, None],
    "drawLine":        [H, V, H, V, None],
    "drawFastHLine":   [H, V, H, None],
    "drawFastVLine":   [H, V, V, None],
    "fillTriangle":    [H, V, H, V, H, V, None],
    "drawTriangle":    [H, V, H, V, H, V, None],
    "setCursor":       [H, V, None],
    "drawPixel":       [H, V, None],
    "fillCircle":      [H, V, None, None],
    "drawCircle":      [H, V, None, None],
    "setViewport":     [H, V, H, V, None],
    "setAddrWindow":   [H, V, H, V],
    "drawBitmap":      [H, V, None, H, V, None],
    "drawXBitmap":     [H, V, None, H, V, None],
    # (text, x, y, font)
    "drawCentreString": [None, H, V, None],
    "drawRightString":  [None, H, V, None],
    "drawString":       [None, H, V, None],
}

MAP = {
    H: {240: "PUEO_SCREEN_W", 120: "(PUEO_SCREEN_W / 2)",
        230: "(PUEO_SCREEN_W - 10)", 220: "(PUEO_SCREEN_W - 20)",
        200: "(PUEO_SCREEN_W - 40)"},
    V: {320: "PUEO_SCREEN_H", 160: "(PUEO_SCREEN_H / 2)",
        304: "(PUEO_SCREEN_H - 16)", 300: "(PUEO_SCREEN_H - 20)"},
}

CALL = re.compile(r"\btft\.(\w+)\s*\(")
BARE_INT = re.compile(r"^\s*(\d{2,3})\s*$")


def split_args(text, start):
    """Arguments of the call whose '(' is at `start`. None if it does not
    close on this line, or if an argument contains a nested comma."""
    depth = 0
    for i in range(start, len(text)):
        if text[i] == "(":
            depth += 1
        elif text[i] == ")":
            depth -= 1
            if depth == 0:
                inner = text[start + 1:i]
                if "(" in inner or "[" in inner:
                    return None, None
                return inner.split(","), i
    return None, None


def main():
    total = 0
    skipped = []
    changes = []

    for name in FILES:
        path = SKETCH / name
        if not path.exists():
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        if "PUEO_SCREEN_W / 2" in src or "PUEO_SCREEN_H - 16" in src:
            continue

        nl = "\r\n" if "\r\n" in src else "\n"
        lines = src.split(nl)
        n = 0

        for li, line in enumerate(lines):
            out = line
            pos = 0
            while True:
                m = CALL.search(out, pos)
                if not m:
                    break
                fn = m.group(1)
                open_at = m.end() - 1
                if fn not in SIGS:
                    pos = m.end()
                    continue
                args, close_at = split_args(out, open_at)
                if args is None:
                    skipped.append(f"{name}:{li+1} {fn}(...)")
                    pos = m.end()
                    continue
                slots = SIGS[fn]
                if len(args) != len(slots):
                    pos = m.end()
                    continue
                touched = False
                for ai, arg in enumerate(args):
                    kind = slots[ai]
                    if kind is None:
                        continue
                    bm = BARE_INT.match(arg)
                    if not bm:
                        continue
                    v = int(bm.group(1))
                    if v in MAP[kind]:
                        args[ai] = arg.replace(bm.group(1), MAP[kind][v])
                        touched = True
                if touched:
                    new = out[:open_at + 1] + ",".join(args) + out[close_at:]
                    changes.append(f"  {name}:{li+1}")
                    changes.append(f"    - {out.strip()[:88]}")
                    changes.append(f"    + {new.strip()[:88]}")
                    out = new
                    n += 1
                pos = m.end()
            lines[li] = out

        if n:
            path.write_text(nl.join(lines), encoding="utf-8", newline="")
            print(f"{name:<16} {n:>3} call(s) rewritten")
            total += n

    print(f"\n{total} drawing call(s) now follow the panel\n")
    for c in changes:
        print(c)

    if skipped:
        print(f"\n{len(skipped)} call(s) skipped -- nested parens or brackets "
              "in the arguments, read these by hand:")
        for s in skipped[:30]:
            print("  " + s)
        if len(skipped) > 30:
            print(f"  ... and {len(skipped) - 30} more")
    return 0


if __name__ == "__main__":
    sys.exit(main())
