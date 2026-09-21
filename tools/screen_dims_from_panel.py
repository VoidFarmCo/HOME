#!/usr/bin/env python3
"""Make every screen dimension follow the panel instead of being 240x320.

One-shot. Run once, recorded here, refuses to run twice.

The menu grid was made panel-aware during bring-up. The feature screens were
not, and on a 3.5" board that showed as every app drawing into the top-left
240x320 of a 320x480 panel: a third of the screen black below the content,
the status-bar icons bunched at 60% of the width because their x positions
were measured against 240, and the scrolling terminal wrapping at row 320.

The dimensions were not in one place to fix. They were in 43:

    28x  constexpr int SCREEN_WIDTH = 240;
    15x  #define SCREENHEIGHT 320
     2x  #define DISPLAY_WIDTH 240 / #define DISPLAY_HEIGHT 320   (x2 files)
     2x  #define TFT_WIDTH 240 / #define TFT_HEIGHT 320           (wifi.cpp)
     1x  #define MAX_Y 320
     1x  #define SCREEN_WIDTH 240  (utils.cpp Terminal)

all spelled as literals, in four files, several of them inside per-screen
blocks that redefine the same name the file already defined earlier. This is
the same shape as the bus-map and pin-map problems before it: one fact,
restated everywhere, corrected in some places and not others.

Every one of them now reads PUEO_SCREEN_W / PUEO_SCREEN_H from shared.h,
which branch on PUEO_PANEL_35. The 2.8" values are unchanged, so a 2.8"
build is byte-compatible with what these literals said.

What this does NOT fix, and what still has to be read by hand: coordinates
DERIVED from 240 rather than equal to it -- icon rows at x = {10, 190, 220},
tile widths, right-aligned labels at width - k. Those are listed at the end
of the run so they can be worked through deliberately rather than guessed at
by a regex.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

MARKER = "PUEO_SCREEN_W"

SHARED_ANCHOR = """/*──────────────────── Display & Touch ────────────────────*/
#ifndef TFT_WIDTH
#define TFT_WIDTH 240
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT 320
#endif"""

SHARED_NEW = """/*──────────────────── Display & Touch ────────────────────*/

/* The panel's dimensions, in one place, for the whole UI.
 *
 * Every feature screen used to carry its own copy of these as literals --
 * 43 of them across four files -- which is why the 3.5" board drew each app
 * into the top-left 240x320 of a 320x480 display and left the rest black.
 *
 * Deliberately not TFT_eSPI's TFT_WIDTH/TFT_HEIGHT: those are only correct
 * once TFT_eSPI.h has been included, and several of these screens compute
 * layout in headers that do not include it. These follow PUEO_PANEL_35
 * directly, which is also what User_Setup branches on, so the two cannot
 * disagree. */
#if PUEO_PANEL_35
#define PUEO_SCREEN_W 320
#define PUEO_SCREEN_H 480
#else
#define PUEO_SCREEN_W 240
#define PUEO_SCREEN_H 320
#endif

#ifndef TFT_WIDTH
#define TFT_WIDTH PUEO_SCREEN_W
#endif
#ifndef TFT_HEIGHT
#define TFT_HEIGHT PUEO_SCREEN_H
#endif"""

# (regex, replacement). Anchored on the whole declaration so a bare 240 or
# 320 somewhere else in the line cannot be caught by accident.
RULES = [
    (re.compile(r"(constexpr int SCREEN_WIDTH = )240(;)"), r"\1PUEO_SCREEN_W\2"),
    (re.compile(r"(#\s*define SCREENHEIGHT )320\b"), r"\1PUEO_SCREEN_H"),
    (re.compile(r"(#\s*define SCREEN_WIDTH )240\b"), r"\1PUEO_SCREEN_W"),
    (re.compile(r"(#\s*define DISPLAY_WIDTH )240\b"), r"\1PUEO_SCREEN_W"),
    (re.compile(r"(#\s*define DISPLAY_HEIGHT )320\b"), r"\1PUEO_SCREEN_H"),
    (re.compile(r"(#\s*define TFT_WIDTH )240\b"), r"\1PUEO_SCREEN_W"),
    (re.compile(r"(#\s*define TFT_HEIGHT )320\b"), r"\1PUEO_SCREEN_H"),
    (re.compile(r"(#\s*define MAX_Y )320\b"), r"\1PUEO_SCREEN_H"),
]

TARGETS = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp",
           "ducky.cpp", "Touchscreen.h"]

# Coordinates that are a function of 240 rather than equal to it. Reported,
# never rewritten: only reading the screen tells you whether 190 is
# "width - 50" or a number that happens to be 190.
SUSPECT = re.compile(
    r"\b(?:iconX|static int iconX)\b.*\{[^}]*\}"
    r"|\bSCREEN_WIDTH\s*-\s*\d+"
    r"|\b(?:1[6-9]\d|2[0-3]\d)\s*,\s*(?:STATUS_BAR_Y_OFFSET|iconY)\b"
)


def main():
    shared = SKETCH / "shared.h"
    text = shared.read_text(encoding="utf-8", errors="replace")

    if MARKER in text:
        print("shared.h already defines PUEO_SCREEN_W -- this has run before.",
              file=sys.stderr)
        print("It is a one-shot: it records how the change was made, not a "
              "thing to run again.", file=sys.stderr)
        return 1

    nl = "\r\n" if "\r\n" in text else "\n"
    anchor = SHARED_ANCHOR.replace("\n", nl)
    if text.count(anchor) != 1:
        print("shared.h's Display & Touch block is not where this expects it; "
              "refusing to guess.", file=sys.stderr)
        return 1
    shared.write_text(text.replace(anchor, SHARED_NEW.replace("\n", nl)),
                      encoding="utf-8", newline="")
    print("shared.h  +PUEO_SCREEN_W / +PUEO_SCREEN_H")

    total = 0
    for name in TARGETS:
        path = SKETCH / name
        if not path.exists():
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        nl = "\r\n" if "\r\n" in src else "\n"
        out, n = src, 0
        for pattern, repl in RULES:
            out, k = pattern.subn(repl, out)
            n += k
        if n:
            path.write_text(out, encoding="utf-8", newline="")
            print(f"{name:<16} {n:>3} dimension(s) now follow the panel")
            total += n

    print(f"\n{total} literal(s) replaced")

    print("\nDerived coordinates this cannot settle -- read these by hand:")
    found = 0
    for name in TARGETS:
        path = SKETCH / name
        if not path.exists():
            continue
        for i, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            if SUSPECT.search(line):
                print(f"  {name}:{i}: {line.strip()[:78]}")
                found += 1
    if not found:
        print("  (none found)")
    return 0


if __name__ == "__main__":
    sys.exit(main())
