#!/usr/bin/env python3
"""The audience profile survives a reboot.

The profile (HOME vs COMBAT) and the profileChosen flag that drives the first-boot
picker both live in AppSettings and both must round-trip through settings.json, or
the device forgets who it is every power cycle and either re-asks the picker forever
or boots the wrong profile.

check_settings.py already asserts each field is "in load" and "in save", but it does
that by substring -- and "profile" is a substring of "profileChosen", so dropping the
real doc["profile"] line while keeping profileChosen would slip past it. This checks
the exact JSON keys in both directions, which is the thing that actually breaks.

    python tools/check_profile_persist.py

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


def func_body(src, sig):
    i = src.find(sig)
    if i < 0:
        return ""
    j = src.find("{", i)
    depth, k = 0, j
    while k < len(src):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j:k]
        k += 1
    return src[j:]


def main():
    h = (SKETCH / "SettingsStore.h").read_text(encoding="utf-8", errors="replace")
    c = (SKETCH / "SettingsStore.cpp").read_text(encoding="utf-8", errors="replace")

    print("the fields exist:")
    ok("AppSettings has a Profile profile field",
       re.search(r"\bProfile\s+profile\s*=", h) is not None)
    ok("AppSettings has a bool profileChosen field",
       re.search(r"\bbool\s+profileChosen\s*=", h) is not None)

    load = func_body(c, "bool settingsLoad()")
    save = func_body(c, "bool settingsSave()")
    ok("found settingsLoad and settingsSave", bool(load) and bool(save))

    print("\nboth keys round-trip (exact JSON key, not a substring):")
    for key in ('doc["profile"]', 'doc["profileChosen"]'):
        ok("%s is read in settingsLoad" % key, key in load)
        ok("%s is written in settingsSave" % key, key in save)

    # The loaded value is assigned back to the struct, not just read and dropped.
    ok("settingsLoad assigns s.profile", re.search(r"s\.profile\s*=", load) is not None)
    ok("settingsLoad assigns s.profileChosen",
       re.search(r"s\.profileChosen\s*=", load) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
