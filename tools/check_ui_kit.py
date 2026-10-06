#!/usr/bin/env python3
"""H.O.M.E has its own UI identity, and it lives in one place.

H.O.M.E is our firmware, not a Pueo reskin, so the screens the user lives in share
one visual kit (home_ui.h): the Profile concept, the per-profile accent and wording.
Two things this holds:

1. The kit exists and is coherent -- Profile has Home and Combat; each profile's
   default accent points at the right preset in kAccentPresets (SettingsStore.cpp),
   cross-checked by NAME so reordering that table cannot silently repoint them; the
   combat title is the product's own and the home title is plain.

2. The new H.O.M.E screens do not reach past the kit for a raw colour. Every screen
   we add (the picker, the playbook home, the status-explain screen) must colour
   itself through the palette / accent helpers, never a bare 0xRRRR literal, so the
   identity cannot drift one file at a time. Checked for each such file that exists;
   it binds as those files are written.

    python tools/check_ui_kit.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

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


def accent_index(name, presets_src):
    """Index of a named preset in kAccentPresets[], or None.

    Scoped to the kAccentPresets array body only -- SettingsStore.cpp has other
    {"name", ...} tables (the log apps) that would otherwise shift the indices.
    """
    m = re.search(r"kAccentPresets\[\]\s*=\s*\{(.*?)\};", presets_src, re.S)
    if not m:
        return None
    order = re.findall(r'\{\s*"([^"]+)"\s*,', m.group(1))
    return order.index(name) if name in order else None


def main():
    kit_path = SKETCH / "home_ui.h"
    ok("home_ui.h exists", kit_path.is_file())
    if not kit_path.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    kit = kit_path.read_text(encoding="utf-8", errors="replace")
    store = (SKETCH / "SettingsStore.cpp").read_text(encoding="utf-8", errors="replace")

    print("the kit is coherent:")
    ok("Profile enum defines Home and Combat",
       re.search(r"enum\s+class\s+Profile\b", kit) is not None
       and "Home" in kit and "Combat" in kit)

    # The two default-accent constants, read from the kit.
    m_home = re.search(r"HOME_UI_ACCENT_HOME\s*=\s*(\d+)", kit)
    m_comb = re.search(r"HOME_UI_ACCENT_COMBAT\s*=\s*(\d+)", kit)
    ok("the kit sets a default accent for each profile",
       m_home is not None and m_comb is not None)

    # Cross-check those indices against the preset table BY NAME: HOME must be the
    # brand purple, COMBAT must be red. A reordering of kAccentPresets that did not
    # update the kit fails here.
    purple = accent_index("Purple", store)
    red = accent_index("Red", store)
    ok("kAccentPresets still has Purple and Red", purple is not None and red is not None,
       "presets: %s" % re.findall(r'\{\s*"([^"]+)"', store))
    if m_home and purple is not None:
        ok("HOME default accent is the brand Purple preset",
           int(m_home.group(1)) == purple,
           "kit=%s purple=%s" % (m_home.group(1), purple))
    if m_comb and red is not None:
        ok("COMBAT default accent is the Red preset",
           int(m_comb.group(1)) == red,
           "kit=%s red=%s" % (m_comb.group(1), red))

    ok("homeUiProfileTitle carries the combat and home names",
       '"HEADS OF MY ENEMIES"' in kit and '"HOME"' in kit)

    print("\nnew H.O.M.E screens colour through the kit, not raw literals:")
    # Files that are H.O.M.E's own screens. Each is checked once it exists; until
    # then this is a standing rule waiting for the file, not a free pass forever.
    screen_files = ["playbooks.cpp", "status_explain.cpp", "profile_picker.cpp"]
    # A 16-bit colour literal in code (not in a // or /* */ comment, not in a string).
    colour_lit = re.compile(r"0x[0-9A-Fa-f]{4}\b")
    for fn in screen_files:
        p = SKETCH / fn
        if not p.is_file():
            ok("%s (not yet written) -- rule stands for when it is" % fn, True)
            continue
        src = p.read_text(encoding="utf-8", errors="replace")
        # strip block and line comments and string contents before scanning
        stripped = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
        stripped = re.sub(r"//[^\n]*", "", stripped)
        stripped = re.sub(r'"(?:\\.|[^"\\])*"', '""', stripped)
        hits = colour_lit.findall(stripped)
        ok("%s uses no raw colour literal" % fn, not hits,
           "found %s" % sorted(set(hits)))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
