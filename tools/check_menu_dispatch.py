#!/usr/bin/env python3
"""Every menu entry is reachable, by touch and by button.

ESP32-DIV.ino dispatches a submenu selection twice: once from the hardware
buttons and once from the touch nav bar. They are two hand-maintained
if/else chains over the same set of indices, and nothing made them agree.

They did not. Bluetooth page 2 declared four entries and the touch chain
stopped after the first, so Spotter, Fast Pair and Hunt drew a tile, took
the tap, highlighted, and did nothing -- on a board whose only input is the
touch panel, three features simply did not exist. The button chain had all
four, which is why it survived a read: the feature was wired, just not from
the way anyone was using it.

check_menu_tables.py already checks that labels and icons line up. This
checks that something happens when you press one.

Reads source. Does not need a board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"

# `if (x_submenu_page == P && current_submenu_index == I) {`, in either
# chain. The `while (...)` inside a branch restates the same pair and is not
# a dispatch, so the match is anchored at the start of the line.
DISPATCH = re.compile(
    r"^(\s*)(?:\}?\s*else\s+)?if \((\w+)_submenu_page == (\d+)"
    r" && current_submenu_index == (\d+)\)")

# The touch chain sits inside the nav-bar handler and so is indented further
# than the button chain's top-level ifs. Splitting on that rather than on
# line number keeps this working when either chain moves.
TOUCH_INDENT = 16

COUNT = re.compile(r"(\w+)_PAGE(\d)_FEATURES = (\d+);")

# The counts and the dispatch variables disagree about what the Bluetooth
# submenu is called -- BT_PAGE1_FEATURES against bluetooth_submenu_page --
# so the two are joined here rather than by hoping they match. An unlisted
# prefix is used as-is, which is what wifi does.
ALIAS = {"bt": "bluetooth"}


def main():
    src = SKETCH.read_text(encoding="utf-8", errors="replace")

    declared = {}
    for m in COUNT.finditer(src):
        prefix = m.group(1).lower()
        declared[(ALIAS.get(prefix, prefix), int(m.group(2)))] = int(m.group(3))

    if not declared:
        print("no *_PAGEn_FEATURES counts found; this check is testing nothing",
              file=sys.stderr)
        return 1

    chains = {}
    for line in src.splitlines():
        m = DISPATCH.match(line)
        if not m:
            continue
        chain = "touch" if len(m.group(1)) >= TOUCH_INDENT else "button"
        key = (m.group(2), int(m.group(3)), chain)
        chains.setdefault(key, set()).add(int(m.group(4)))

    failed = 0
    checks = 0
    for (var, page), n in sorted(declared.items()):
        for chain in ("button", "touch"):
            checks += 1
            have = chains.get((var, page, chain), set())
            missing = [i for i in range(n) if i not in have]
            name = f"{var} page {page + 1}, {chain}"
            if missing:
                labels = ", ".join(str(i) for i in missing)
                print(f"  FAIL  {name:<28} {n} entries, no dispatch for {labels}")
                failed += 1
            else:
                print(f"  ok    {name:<28} all {n} entries dispatch")

    # A dispatch for an index the page does not have is the same bug from the
    # other end: an entry was removed and its handler left behind, where it
    # will fire for whatever takes that slot next.
    for (var, page, chain), have in sorted(chains.items()):
        n = declared.get((var, page))
        if n is None:
            continue
        checks += 1
        stray = [i for i in sorted(have) if i >= n]
        if stray:
            print(f"  FAIL  {var} page {page + 1}, {chain:<7} dispatches "
                  f"{stray} but the page has {n} entries")
            failed += 1
        else:
            print(f"  ok    {var} page {page + 1}, {chain:<7} has no orphaned handler")

    print()
    if failed:
        print(f"FAILED: {failed} of {checks}")
        return 1
    print(f"{checks} checks passed -- every entry dispatches from both chains")
    return 0


if __name__ == "__main__":
    sys.exit(main())
