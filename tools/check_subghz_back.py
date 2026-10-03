#!/usr/bin/env python3
"""The SubGHz submenu's Back is the last item, not a hardcoded index.

handleSubGHzSubmenuButtons decided which tile is "Back to Main Menu" with a
literal `current_submenu_index == 5`, because SubGHz had six entries and Back
was index 5. Adding SubGHz Chat made Back index 6, so Back fell through to
launchSubGhzFeature(6) (no case -> no-op) and the menu could not be left, while
index 5 (now the Chat) triggered the old Back branch. check_menu_dispatch does
not model the back index, so it did not catch this.

The other submenus use a dynamic back index (pagedBackBtnIndex / a backIdx
param). SubGHz must too: the Back item is active_submenu_size - 1, never a
literal.

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

INO = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    src = INO.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"void\s+handleSubGHzSubmenuButtons\s*\(\)\s*\{.*?\n\}", src, re.S)
    body = m.group(0) if m else ""
    ok("handleSubGHzSubmenuButtons found", bool(body))

    # The bug: a literal index used as the Back test.
    ok("Back is not a hardcoded submenu index",
       re.search(r"current_submenu_index\s*[!=]=\s*\d+", body) is None,
       "a literal Back index breaks when the menu grows")

    # The fix: the Back index is derived from the menu size.
    ok("Back is derived from the menu size",
       re.search(r"active_submenu_size\s*-\s*1", body) is not None,
       "Back should be the last item (active_submenu_size - 1)")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
