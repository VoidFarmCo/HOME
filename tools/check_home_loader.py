#!/usr/bin/env python3
"""The loading animation is H.O.M.E's crowned skull, not upstream's.

loading() is the spinner shown on the boot splash and on every feature's init
screen. It used to cycle ten upstream skull_loading frames; it now cycles the
H.O.M.E crowned-skull dissolve (bits fall off the bottom, rise back, reassemble).
The ways that regresses, each caught here:

  1. The spinner must reference the home_load frames, and none of the upstream
     bitmap_icon_skull_loading frames -- a stray reference means the fork's own
     loader sits unused while Pueo's skull still plays somewhere.

  2. numFrames must equal the number of home_load arrays that exist. Too high
     and loading() walks off the array (draws garbage / reads past the end);
     too low and the tail of the animation never shows.

Whether the boot splash plays a pass (PUEO_BOOT_SKULL_REPEATS) is a preference,
not an invariant -- the owner chose a logo-only boot -- so it is not checked.

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

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


def main():
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    icon = (SKETCH / "icon.h").read_text(encoding="utf-8", errors="replace")

    # 1a. loading() references the home_load frames.
    load = re.search(r"void\s+loading\s*\([^)]*\)\s*\{.*?\n\}", utils, re.S)
    body = load.group(0) if load else ""
    ok("loading() found", bool(body))
    home_refs = set(re.findall(r"bitmap_home_load_\d+", body))
    ok("loading() cycles the home_load frames", len(home_refs) >= 2,
       "found %d" % len(home_refs))

    # 1b. no upstream skull_loading frame is referenced anywhere in code.
    code = ""
    for p in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino")):
        code += p.read_text(encoding="utf-8", errors="replace")
    ok("no upstream skull_loading frame is referenced",
       re.search(r"bitmap_icon_skull_loading", code) is None,
       "the Pueo loader is still wired in somewhere")

    # 2. numFrames == number of home_load arrays defined.
    m = re.search(r"const\s+int\s+numFrames\s*=\s*(\d+)", body)
    numFrames = int(m.group(1)) if m else -1
    defined = len(set(re.findall(r"bitmap_home_load_(\d+)\s*\[\]\s*PROGMEM", icon)))
    ok("numFrames matches the frames that exist",
       numFrames == defined and defined > 0,
       "numFrames=%d but %d home_load arrays defined" % (numFrames, defined))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
