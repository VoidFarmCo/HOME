#!/usr/bin/env python3
"""The feature title marks are H.O.M.E's crowned skull, not upstream's.

Opening Drone Detector, Surveillance, or Hunt shows a 200x200 title mark for a
beat. Those were upstream's three marks (bitmap_pueo_drone / _spotter / _hunt);
they now show the one H.O.M.E crowned-skull mark (bitmap_home_mark). The ways
that regresses, each caught here:

  1. All three feature-open marks reference bitmap_home_mark, and none of the
     three upstream marks is referenced anywhere in code -- a stray reference
     means a feature still opens on Pueo's art.

  2. bitmap_home_mark is defined at the mark size (200x200 = 5000 bytes), so the
     reference resolves and draws the whole mark rather than running short.

Not checked: bitmap_pueo_dwell. That is the Surveillance dwell-ALERT indicator,
a different thing from a feature title mark, left as its own decision.

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
    code = ""
    for p in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino")):
        code += p.read_text(encoding="utf-8", errors="replace")
    icon = (SKETCH / "icon.h").read_text(encoding="utf-8", errors="replace")

    # 1. the three feature-open marks use home_mark; no upstream mark referenced.
    for nm, feat in [("Drone Detector", "Drone Detector"),
                     ("Surveillance", "Surveillance"),
                     ("Hunt", "Hunt")]:
        ok("%s opens on bitmap_home_mark" % feat,
           re.search(r"showFeatureMark\s*\(\s*bitmap_home_mark\s*,\s*\"%s\"" % re.escape(nm), code) is not None)
    for up in ("bitmap_pueo_drone", "bitmap_pueo_spotter", "bitmap_pueo_hunt"):
        ok("no reference to %s" % up, up not in code,
           "a feature still opens on upstream art")

    # 2. home_mark defined at the mark size.
    m = re.search(r"bitmap_home_mark\s*\[\]\s*PROGMEM\s*=\s*\{(.*?)\}", icon, re.S)
    nbytes = len(re.findall(r"0x[0-9a-fA-F]+", m.group(1))) if m else 0
    ok("bitmap_home_mark is defined at 200x200 (5000 bytes)", nbytes == 5000,
       "found %d bytes" % nbytes)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
