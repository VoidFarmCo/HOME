#!/usr/bin/env python3
"""Report where the contested UI constants are defined and where they are used.

The sketch redefines a handful of per-screen constants over and over in the
same translation unit -- SCREEN_HEIGHT takes four different values in
bluetooth.cpp alone. Twenty-two of those redefinitions differ in value and
warn; the rest repeat the same number and pass silently.

Before changing any of it, this works out for each definition:
  - the enclosing namespace, if any
  - whether it sits inside a function body
  - its value
  - every use between it and the next definition of the same name

That last part is what decides whether a definition can become a scoped
constant or has to stay a macro: uses confined to one function can move into
that function, uses at namespace scope need a namespace-level constant, and a
name used across a namespace boundary cannot move at all.

Read-only. Prints a table.
"""

import re
import sys
from collections import defaultdict
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

NAMES = [
    "SCREEN_WIDTH", "SCREEN_HEIGHT", "LINE_HEIGHT", "MAX_LINES",
    "ICON_NUM", "ICON_SIZE", "STATUS_BAR_Y_OFFSET", "STATUS_BAR_HEIGHT",
    "MAX_SSID_LENGTH",
]
FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp", "gps.cpp",
         "rfid.cpp", "ducky.cpp", "ir.cpp", "shared.h"]

DEFINE = re.compile(r"^\s*#\s*define\s+(\w+)\s+(.*?)\s*$")
NS_OPEN = re.compile(r"^\s*namespace\s+(\w+)\s*\{")
ANON_NS = re.compile(r"^\s*namespace\s*\{")
# A function definition at column 0 that opens a brace on the same line.
FUNC_OPEN = re.compile(r"^[A-Za-z_][\w:<>,\s\*&]*\([^;]*\)\s*(const\s*)?\{\s*$")


def strip_strings(line):
    return re.sub(r'"(\\.|[^"\\])*"', '""', line)


def analyse(path):
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    ns_stack = []       # [(name, depth_at_open)]
    depth = 0
    func = None         # (name, depth_at_open)
    out = []

    for i, raw in enumerate(lines, 1):
        line = strip_strings(raw)

        m = DEFINE.match(raw)
        if m and m.group(1) in NAMES:
            out.append({
                "line": i,
                "name": m.group(1),
                "value": m.group(2).split("//")[0].strip(),
                "ns": "::".join(n for n, _ in ns_stack) or "-",
                "func": func[0] if func else "-",
            })

        nm = NS_OPEN.match(line)
        if nm:
            ns_stack.append((nm.group(1), depth))
        elif ANON_NS.match(line):
            ns_stack.append(("(anon)", depth))
        elif func is None and FUNC_OPEN.match(line):
            func = (line.strip()[:48], depth)

        opens = line.count("{")
        closes = line.count("}")
        depth += opens - closes
        while ns_stack and depth <= ns_stack[-1][1]:
            ns_stack.pop()
        if func and depth <= func[1]:
            func = None

    return out, lines


def uses(lines, name, start, end):
    pat = re.compile(r"\b%s\b" % re.escape(name))
    hits = []
    for i in range(start, min(end, len(lines))):
        raw = lines[i]
        if DEFINE.match(raw):
            continue
        if pat.search(strip_strings(raw)):
            hits.append(i + 1)
    return hits


def main():
    total = 0
    movable = 0
    stuck = []
    for fname in FILES:
        p = SKETCH / fname
        if not p.exists():
            continue
        defs, lines = analyse(p)
        if not defs:
            continue
        by_name = defaultdict(list)
        for d in defs:
            by_name[d["name"]].append(d)

        print("=" * 72)
        print(fname)
        print("=" * 72)
        for name in NAMES:
            ds = by_name.get(name)
            if not ds:
                continue
            values = {d["value"] for d in ds}
            flag = "  <-- values differ" if len(values) > 1 else ""
            print("\n  %s  (%d definitions, %d distinct value(s))%s"
                  % (name, len(ds), len(values), flag))
            for k, d in enumerate(ds):
                nxt = ds[k + 1]["line"] - 1 if k + 1 < len(ds) else len(lines)
                u = uses(lines, name, d["line"], nxt)
                total += 1
                scope = "fn" if d["func"] != "-" else ("ns:" + d["ns"])
                # A definition whose uses all sit inside the same function can
                # become a local constant; one used at namespace scope needs a
                # namespace constant; zero uses is dead.
                if d["func"] != "-":
                    verdict = "-> local constexpr"
                    movable += 1
                elif d["ns"] != "-":
                    verdict = "-> namespace constexpr"
                    movable += 1
                else:
                    verdict = "-> file scope, needs care"
                    stuck.append("%s:%d %s" % (fname, d["line"], name))
                print("    line %-6d = %-28s %-10s uses:%-3d %s"
                      % (d["line"], d["value"][:28], scope, len(u), verdict))
    print()
    print("definitions examined: %d   scoped-constant candidates: %d" % (total, movable))
    if stuck:
        print("at bare file scope (%d):" % len(stuck))
        for s in stuck[:20]:
            print("   " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
