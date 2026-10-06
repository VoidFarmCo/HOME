#!/usr/bin/env python3
"""The first-boot picker runs once, before the menu, and records its answer.

Three ways this feature breaks silently, each checked here:

1. The picker is never reached -- setup() calls the menu without first asking.
   Then a fresh device boots straight into HOME and nobody is ever asked.
2. It is reached unconditionally -- the !profileChosen guard is gone, so the
   picker blocks every boot forever, including after a choice was made.
3. It asks but never records -- the picker does not set profileChosen / save,
   so the guard is always false and it re-asks on every boot.

    python tools/check_profile_picker.py

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
                return src[j:k + 1]
        k += 1
    return src[j:]


def main():
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    picker_path = SKETCH / "profile_picker.cpp"
    ok("profile_picker.cpp exists", picker_path.is_file())
    if not picker_path.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    picker = picker_path.read_text(encoding="utf-8", errors="replace")

    setup = func_body(ino, "void setup()")
    ok("found setup()", bool(setup))

    print("\nthe picker is reached, guarded, and before the menu:")
    guard = re.search(r"!\s*settings\(\)\.profileChosen", setup)
    ok("setup() guards on !settings().profileChosen", guard is not None)
    call = re.search(r"ProfilePicker::run\s*\(\s*\)", setup)
    ok("setup() calls ProfilePicker::run()", call is not None)
    menu = re.search(r"\bdrawPlaybookHome\s*\(\s*\)", setup)
    ok("setup() lands on the purpose-tile home", menu is not None)
    if guard and call and menu:
        # The guard and the run() call must come before the landing home draw,
        # and the guard must sit right before the call (same if).
        ok("the picker runs before the landing home",
           guard.start() < menu.start() and call.start() < menu.start())
        ok("the call is guarded (guard precedes the call, close by)",
           0 <= call.start() - guard.start() < 120,
           "guard@%d call@%d" % (guard.start(), call.start()))

    print("\nthe profile is switchable at runtime (System > Profile):")
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    ok("a Profile settings row exists (kProfileRow)",
       re.search(r"\bkProfileRow\b\s*=", utils) is not None)
    ok('the row is labelled "Profile"', '"Profile"' in utils)
    ok("the settings UI re-opens the picker to switch",
       re.search(r"ProfilePicker::run\s*\(\s*\)", utils) is not None)

    print("\nthe picker records its answer (so it asks once):")
    ok("sets profileChosen = true",
       re.search(r"\.profileChosen\s*=\s*true", picker) is not None)
    ok("sets the chosen profile",
       re.search(r"\.profile\s*=\s*chosen", picker) is not None)
    ok("persists with settingsSave()",
       re.search(r"settingsSave\s*\(\s*\)", picker) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
