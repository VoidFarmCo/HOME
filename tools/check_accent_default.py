#!/usr/bin/env python3
"""H.O.M.E opens on its brand purple accent, not inherited orange.

The tile icons, warnings and battery meter draw in the user-selectable accent
(settings().accentColor -> accentColor565). Two things make that the brand:

  1. The default accentColor is the Purple preset's index, so a fresh install
     accents purple rather than ESP32-DIV's orange (index 0). The index is read
     from the preset table, not hard-coded, so reordering the presets moves this
     check with it.

  2. The Purple preset is the brand purple (== UI_ACCENT in shared.h), not the
     harsh magenta it shipped as (0xF81F). A user who picks Purple gets the same
     color the rest of the fork's own screens already use.

Reads source. Needs no board. (Only the default is checked; the accent stays
user-switchable, and a device with a saved setting keeps its choice.)
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
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    store_c = (SKETCH / "SettingsStore.cpp").read_text(encoding="utf-8", errors="replace")
    store_h = (SKETCH / "SettingsStore.h").read_text(encoding="utf-8", errors="replace")
    shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")

    # preset table order -> Purple's index
    presets = re.findall(r'\{\s*"([^"]+)"\s*,\s*(0x[0-9a-fA-F]+)\s*\}', store_c)
    names = [p[0] for p in presets]
    ok("accent preset table found", len(presets) >= 2)
    purple_idx = names.index("Purple") if "Purple" in names else -1
    ok("there is a Purple preset", purple_idx >= 0)

    # ACCENT_PRESET_COUNT must match the table length, or the Settings cycle
    # (next = (i + 1) % ACCENT_PRESET_COUNT) skips a preset or clamps a valid
    # one away. This caught nothing until Tan/Olive were added -- now it pins it.
    mc = re.search(r"ACCENT_PRESET_COUNT\s*=\s*(\d+)", store_h)
    count = int(mc.group(1)) if mc else -1
    ok("ACCENT_PRESET_COUNT matches the preset table length",
       count == len(presets), "count=%d, table=%d" % (count, len(presets)))
    ok("Tan and Olive presets are present",
       "Tan" in names and "Olive" in names, "names=%s" % names)

    # 1. default accentColor == Purple index
    m = re.search(r"accentColor\s*=\s*(\d+)", store_h)
    default = int(m.group(1)) if m else -1
    ok("default accent is the Purple preset", default == purple_idx and purple_idx >= 0,
       "default=%d, Purple index=%d" % (default, purple_idx))

    # 2. Purple preset == UI_ACCENT (brand purple)
    ma = re.search(r"#define\s+UI_ACCENT\s+(0x[0-9a-fA-F]+)", shared)
    brand = ma.group(1).lower() if ma else None
    purple_col = presets[purple_idx][1].lower() if purple_idx >= 0 else None
    ok("Purple preset is the brand purple (== UI_ACCENT)",
       brand is not None and purple_col == brand,
       "Purple=%s, UI_ACCENT=%s" % (purple_col, brand))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
