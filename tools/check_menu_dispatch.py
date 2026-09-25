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

    # ---- a branch must pin the index it is a branch for ------------------
    #
    # Every paged branch opens with `current_submenu_index = N;` and its
    # while loop restates it. Renumbering a branch without renumbering those
    # is silent and total: the while condition is false on the first test so
    # the feature's loop never runs, and control falls out with the index
    # naming a different entry, which the next pass then launches.
    #
    # That shipped. ARP Scanner pinned 1 while branching on 3, so it exited
    # into Hidden SSID Revealer. The check above stayed green throughout,
    # because every entry did still have a branch in both chains.
    text = SKETCH.read_text(encoding="utf-8")
    src_lines = text.split("\n")
    head = re.compile(r"(\w+)_submenu_page == (\d+) && current_submenu_index == (\d+)")
    pin = re.compile(r"^\s*current_submenu_index = (\d+);\s*$")

    mismatched = []
    pinned = 0
    for i, ln in enumerate(src_lines[:-1]):
        h = head.search(ln)
        if not h:
            continue
        t = ln.strip()
        if not (t.startswith("if (") or t.startswith("} else if (")
                or t.startswith("while (")):
            continue
        p = pin.match(src_lines[i + 1])
        if not p:
            continue
        pinned += 1
        if int(p.group(1)) != int(h.group(3)):
            mismatched.append("line %d: %s page %d branch %s pins %s"
                              % (i + 2, h.group(1), int(h.group(2)) + 1,
                                 h.group(3), p.group(1)))

    # Some branches keep themselves running with a bare
    # `while (current_submenu_index == N && ...)`, with no page in it. The
    # first version of this check required `_submenu_page ==` in the header
    # and so could not see those at all, which is how Hidden SSID Revealer
    # kept a while loop testing 7 after moving to index 1 and never ran its
    # own loop once.
    bare = re.compile(r"^\s*while \(current_submenu_index == (\d+) &&")
    branch_at = re.compile(r"^\s*(?:\}?\s*else\s+)?if \((\w+)_submenu_page == (\d+)"
                           r" && current_submenu_index == (\d+)\)")
    for i, ln in enumerate(src_lines):
        b = bare.match(ln)
        if not b:
            continue
        owner = None
        for j in range(i - 1, max(-1, i - 60), -1):
            o = branch_at.match(src_lines[j])
            if o:
                owner = o
                break
        if not owner:
            continue
        pinned += 1
        if int(b.group(1)) != int(owner.group(3)):
            mismatched.append("line %d: %s page %d branch %s loops on %s"
                              % (i + 1, owner.group(1),
                                 int(owner.group(2)) + 1, owner.group(3),
                                 b.group(1)))

    checks += 1
    if mismatched:
        print("  FAIL  a branch pins or loops on an index it is not for")
        for m in mismatched[:8]:
            print("          " + m)
        failed += 1
    else:
        print(f"  ok    all {pinned} branch pins match their own index")

    # ---- the menus that dispatch through one launch function ------------
    #
    # NRF24 and SubGHz used to repeat a ~28-line feature launch per entry,
    # twice over, and this script could not see them at all: it matches on
    # `<menu>_submenu_page ==`, which only the paged menus have. Six of the
    # eight menus had no coverage here, including the two whose entries were
    # copied the most.
    #
    # They now go through launchNrfFeature / launchSubGhzFeature, so what is
    # worth checking changed shape. Not "do both chains have a branch" --
    # there is one branch now -- but "does the switch cover every entry the
    # table declares, and do both chains actually call it".
    text = SKETCH.read_text(encoding="utf-8")
    LAUNCH = {
        "launchNrfFeature": ("nrf_NUM_SUBMENU_ITEMS", "handleNRFSubmenuButtons"),
        "launchSubGhzFeature": ("subghz_NUM_SUBMENU_ITEMS",
                                "handleSubGHzSubmenuButtons"),
    }
    for fn, (count_name, handler) in LAUNCH.items():
        m = re.search(r"const int " + count_name + r" = (\d+);", text)
        body = re.search(r"static void " + fn + r"\(int idx\) \{(.*?)\n\}",
                         text, re.S)
        checks += 1
        if not m or not body:
            print(f"  FAIL  {fn}  not found, or its item count is missing")
            failed += 1
            continue
        # The last table entry is Back, which the caller handles rather than
        # the switch.
        want = set(range(int(m.group(1)) - 1))
        have = {int(x) for x in re.findall(r"case (\d+):", body.group(1))}
        if want == have:
            print(f"  ok    {fn:<20} covers all {len(want)} entries")
        else:
            print(f"  FAIL  {fn:<20} missing {sorted(want - have)}, "
                  f"stray {sorted(have - want)}")
            failed += 1

        # Both chains have to reach it. One caller would be the original bug
        # wearing a new shape.
        hbody = re.search(r"void " + handler + r"\(\) \{(.*?)\n\}", text, re.S)
        inner = hbody.group(1) if hbody else ""
        # Commented-out lines do not count. The first version counted raw
        # text, so commenting a call out left this passing -- which is the
        # edit somebody makes while debugging and forgets to undo.
        live = [ln for ln in inner.split("\n")
                if not ln.strip().startswith(("//", "/*", "*"))]
        n_calls = len(re.findall(fn + r"\(current_submenu_index\)",
                                 "\n".join(live)))
        checks += 1
        if n_calls == 2:
            print(f"  ok    {fn:<20} called from both chains")
        else:
            print(f"  FAIL  {fn:<20} called {n_calls} time(s), expected 2")
            failed += 1

    print()
    if failed:
        print(f"FAILED: {failed} of {checks}")
        return 1
    print(f"{checks} checks passed -- every entry dispatches from both chains")
    return 0


if __name__ == "__main__":
    sys.exit(main())
