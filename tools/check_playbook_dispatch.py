#!/usr/bin/env python3
"""Every tile tool is real, curated, and reachable; the purpose-tile home is the landing.

The home is six PURPOSE tiles; each holds tools pulled across radios. Ways this rots:

1. A tool for no profile (mask 0) -- dead weight that never shows, or a typo hiding
   a tool meant to appear.
2. A tool whose category has no launcher in toolRun() -- tapping it does nothing.
3. A tool in a group that does not exist.
4. The old radio menu creeping back as the landing (an "All Tools" tile, or setup
   dropping into displayMenu() instead of the purpose-tile home).

    python tools/check_playbook_dispatch.py

Reads source; needs no board.
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
    src = INO.read_text(encoding="utf-8", errors="replace")

    groups = re.findall(r'"([^"]+)"', src[src.index("kGroupName[GRP_COUNT]"):][:400])
    ok("six purpose groups are named", "GRP_COUNT" in src and len(groups) >= 6,
       "%d names" % len(groups))

    m = re.search(r"kTools\[\]\s*=\s*\{(.*?)\};", src, re.S)
    ok("kTools registry exists", m is not None)
    if not m:
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    rows = re.findall(
        r'\{\s*"([^"]+)"\s*,\s*(GRP_\w+)\s*,\s*([^,]+?)\s*,\s*(\d+)\s*,\s*([A-Za-z0-9_]+)\s*\}',
        m.group(1))
    ok("the registry has tools", len(rows) >= 8, "%d tools" % len(rows))

    toolrun = func_body(src, "void toolRun(")
    cats_dispatched = set(int(c) for c in re.findall(r"case\s+(\d+)\s*:", toolrun))
    valid_groups = set(re.findall(r"\bGRP_[A-Z]+\b", src[:src.index("kGroupName")] + src))

    print("\nevery tool: a profile, a known group, a dispatched category:")
    bad = 0
    for name, grp, mask, cat, idx in rows:
        if "PB_HOME" not in mask and "PB_COMBAT" not in mask and "PB_BOTH" not in mask:
            bad += 1; ok('"%s" shows for a profile' % name, False, "mask=%s" % mask.strip())
        if grp not in valid_groups:
            bad += 1; ok('"%s" group exists' % name, False, grp)
        if int(cat) not in cats_dispatched:
            bad += 1; ok('"%s" category %s is dispatched by toolRun' % (name, cat), False)
    ok("all %d tools valid" % len(rows), bad == 0, "%d problems" % bad)

    print("\nthe old radio menu is retired; the purpose-tile home is the landing:")
    code = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    code = re.sub(r"//[^\n]*", "", code)
    ok('no "All Tools" tile (ignoring comments)', '"All Tools"' not in code)
    ok("setup() lands on the purpose-tile home",
       "in_playbook_home = true" in src and "drawPlaybookHome()" in src)
    ok("handleButtons routes to the home first",
       re.search(r"if\s*\(\s*in_playbook_home\s*\)\s*\{\s*handlePlaybookHome", src) is not None)
    ok("the profile tag toggles (home header)",
       re.search(r"s_group\s*<\s*0", src) is not None and "applyAccent" in src)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
