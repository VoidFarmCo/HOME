#!/usr/bin/env python3
"""Delete the functions that were marked __attribute__((unused)).

They were marked rather than deleted while the fork still tracked upstream:
they cost nothing (the platform builds -ffunction-sections and links
--gc-sections, so none of them reach the image) and deleting them would have
meant a conflict on every merge. Pueo is standalone now, so the trade flips
and they go.

Removing a function means finding its closing brace, which means tracking
string literals, character literals, and both comment styles -- a plain brace
count walks straight into `'{'` or a brace inside a comment and deletes the
rest of the file. The scanner below handles those.

Declarations (lines ending `;`) are single-line deletions. Definitions are
removed from the marker through the matching close brace, plus any doc
comment immediately above.

Run from the repo root. Prints what it removed.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
MARK = "__attribute__((unused))"


def find_close(text, open_idx):
    """Index just past the brace matching the one at open_idx."""
    depth = 0
    i = open_idx
    n = len(text)
    while i < n:
        c = text[i]
        nxt = text[i + 1] if i + 1 < n else ""

        if c == "/" and nxt == "/":
            j = text.find("\n", i)
            i = n if j < 0 else j
            continue
        if c == "/" and nxt == "*":
            j = text.find("*/", i + 2)
            i = n if j < 0 else j + 2
            continue
        if c == '"':
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == '"':
                    i += 1
                    break
                i += 1
            continue
        if c == "'":
            i += 1
            while i < n:
                if text[i] == "\\":
                    i += 2
                    continue
                if text[i] == "'":
                    i += 1
                    break
                i += 1
            continue

        if c == "{":
            depth += 1
        elif c == "}":
            depth -= 1
            if depth == 0:
                return i + 1
        i += 1
    return -1


def line_start(text, idx):
    j = text.rfind("\n", 0, idx)
    return 0 if j < 0 else j + 1


def swallow_doc_comment(text, start):
    """Extend `start` upwards over a /** ... */ or // block directly above."""
    while True:
        prev_end = text.rfind("\n", 0, start - 1)
        prev_start = 0 if prev_end < 0 else prev_end + 1
        line = text[prev_start:start].strip()
        if line.startswith("//") or (line.startswith("/*") and line.endswith("*/")):
            start = prev_start
            continue
        if line.endswith("*/") and "/*" in line:
            start = prev_start
            continue
        return start


def remove_at(path, line_no):
    """Delete the function whose definition begins on `line_no` (1-based).

    Used for the cascade: removing a dead function often strips the last
    caller from a helper, which the next build then reports as unused too.
    """
    raw = path.read_bytes()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    text = raw.decode("utf-8", "surrogateescape").replace("\r\n", "\n")
    lines = text.split("\n")
    if line_no - 1 >= len(lines):
        return None
    start = sum(len(l) + 1 for l in lines[:line_no - 1])

    brace = text.find("{", start)
    if brace < 0:
        return None
    close = find_close(text, brace)
    if close < 0:
        return None
    end = text.find("\n", close)
    end = len(text) if end < 0 else end + 1

    sig = re.sub(r"\s+", " ", text[start:brace]).strip()
    start = swallow_doc_comment(text, start)
    n = text[start:end].count("\n")
    text = text[:start] + text[end:]
    out = text.replace("\n", nl) if nl == "\r\n" else text
    path.write_bytes(out.encode("utf-8", "surrogateescape"))
    return (n, sig[:70])


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--at":
        # tools/drop_dead_functions.py --at wifi.cpp:8296 rfid.cpp:1239 ...
        # Deletions shift line numbers, so each file's targets are applied
        # bottom-up and one file at a time.
        targets = {}
        for spec in sys.argv[2:]:
            fname, ln = spec.rsplit(":", 1)
            targets.setdefault(fname, []).append(int(ln))
        total = 0
        for fname, lns in targets.items():
            path = SKETCH / fname
            for ln in sorted(lns, reverse=True):
                got = remove_at(path, ln)
                if got:
                    print("  %-16s %3d lines  %s" % (fname, got[0], got[1]))
                    total += got[0]
                else:
                    print("  ! %s:%d did not resolve to a function" % (fname, ln),
                          file=sys.stderr)
        print("\nremoved %d lines" % total)
        return 0

    removed = []
    for path in sorted(SKETCH.glob("*.cpp")):
        raw = path.read_bytes()
        nl = "\r\n" if b"\r\n" in raw else "\n"
        text = raw.decode("utf-8", "surrogateescape").replace("\r\n", "\n")

        changed = True
        while changed:
            changed = False
            idx = text.find(MARK)
            if idx < 0:
                break
            start = line_start(text, idx)

            semi = text.find(";", idx)
            brace = text.find("{", idx)
            if semi >= 0 and (brace < 0 or semi < brace):
                end = text.find("\n", semi)
                end = len(text) if end < 0 else end + 1
                kind = "declaration"
            else:
                close = find_close(text, brace)
                if close < 0:
                    print("  ! unbalanced braces after %s:%d" %
                          (path.name, text[:idx].count("\n") + 1), file=sys.stderr)
                    return 1
                end = text.find("\n", close)
                end = len(text) if end < 0 else end + 1
                kind = "definition"

            sig = text[start:min(start + 110, end)].replace(MARK, "").strip()
            sig = re.sub(r"\s+", " ", sig).split("{")[0].strip()

            start = swallow_doc_comment(text, start)
            lines = text[start:end].count("\n")
            text = text[:start] + text[end:]
            removed.append((path.name, kind, lines, sig[:70]))
            changed = True

        out = text.replace("\n", nl) if nl == "\r\n" else text
        path.write_bytes(out.encode("utf-8", "surrogateescape"))

    if not removed:
        print("nothing marked __attribute__((unused)); nothing to do")
        return 0

    total = sum(r[2] for r in removed)
    for name, kind, lines, sig in removed:
        print("  %-16s %-11s %3d lines  %s" % (name, kind, lines, sig))
    print()
    print("removed %d items, %d lines" % (len(removed), total))
    return 0


if __name__ == "__main__":
    sys.exit(main())
