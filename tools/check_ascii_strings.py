#!/usr/bin/env python3
"""Nothing the panel draws contains a character the font cannot draw.

TFT_eSPI's font 1 is a 96-glyph table covering 0x20 to 0x7E. A character
outside that is UTF-8 in the source, so it reaches drawString() as two or
three bytes, each indexed into the table separately, and what appears on the
panel is two or three wrong glyphs.

    injDrawStatusValue(0, "none - run Scan", UI_DIM_TEXT);

That one is an em dash, which is E2 80 94, and on screen it is three pieces
of line-drawing noise in the middle of a sentence telling somebody what to do
next. rfid.cpp had twenty of them across the clone and read flows, where the
status line is the entire interface.

    python tools/check_ascii_strings.py

Reads source; needs no board.

Comments are stripped first. A box-drawing run in a comment banner is not
drawn by anything, and counting those buries the twenty that are real under
forty that are not.

Serial output is checked too, more weakly: a terminal usually copes with
UTF-8, so a dash there is a portability question rather than a visible fault.
It is reported separately and does not fail the check.
"""
import glob
import io
import os
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

SOURCES = []
for pat in ("ESP32-DIV/*.cpp", "ESP32-DIV/*.h", "ESP32-DIV/*.ino",
            "PueoBeacon/*.cpp", "PueoBeacon/*.h", "PueoBeacon/*.ino"):
    SOURCES += sorted(ROOT.glob(pat))

# Anything that puts text on the panel. The String overloads matter as much
# as the literals: a literal reaches them either way.
DRAWERS = (r"drawString|drawCentreString|drawRightString|drawNumber|"
           r"drawFloat|uiShowLine|drawRow|injDrawStatusValue|"
           r"rfidStatusLine|rfidSetStatus|rfidListenIso14443a|"
           r"rfidShow|setStatus|showStatus|drawStatus|drawLabel|"
           r"drawCentered|drawText|tft\.print")

SERIAL = r"Serial\.print|Serial\.write|log_[ediwv]"

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


def strip_comments(src):
    """Blank comments and keep every other character where it was.

    Offsets have to survive, because the line number and the surrounding
    context are what say whether a string is drawn or printed.
    """
    out = []
    i, n = 0, len(src)
    while i < n:
        c = src[i]
        if c == '"' or c == "'":
            q = c
            j = i + 1
            while j < n:
                if src[j] == "\\":
                    j += 2
                    continue
                if src[j] == q:
                    j += 1
                    break
                j += 1
            out.append(src[i:j])
            i = j
        elif src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(c)
            i += 1
    return "".join(out)


STR = re.compile(r'"((?:[^"\\\n]|\\.)*)"')


def scan():
    drawn, printed, other = [], [], []
    for p in SOURCES:
        raw = io.open(p, encoding="utf-8", errors="replace").read()
        src = strip_comments(raw)
        for m in STR.finditer(src):
            s = m.group(1)
            odd = sorted({c for c in s if ord(c) < 32 or ord(c) > 126})
            odd = [c for c in odd if c not in "\t"]
            if not odd:
                continue
            ln = src[:m.start()].count("\n") + 1
            # the statement this literal belongs to
            lo = src.rfind(";", 0, m.start()) + 1
            lo = max(lo, src.rfind("\n", 0, max(0, m.start() - 400)))
            stmt = src[max(0, lo):m.end() + 80]
            row = (os.path.basename(str(p)), ln, "".join(odd),
                   " ".join(s.split())[:58])
            if re.search(DRAWERS, stmt):
                drawn.append(row)
            elif re.search(SERIAL, stmt):
                printed.append(row)
            else:
                other.append(row)
    return drawn, printed, other


def main():
    drawn, printed, other = scan()

    print("strings the panel draws:")
    for f, ln, odd, s in drawn:
        print("    %-18s:%-5d %-6s %s" % (f, ln, repr(odd), s))
    ok("every drawn string is printable ASCII", not drawn,
       "%d of them carry a character font 1 has no glyph for" % len(drawn))

    print()
    print("strings that go to the serial console (reported, not failed):")
    for f, ln, odd, s in printed:
        print("    %-18s:%-5d %-6s %s" % (f, ln, repr(odd), s))
    if not printed:
        print("    none")

    print()
    print("string literals whose destination this could not tell:")
    for f, ln, odd, s in other:
        print("    %-18s:%-5d %-6s %s" % (f, ln, repr(odd), s))
    # These are the dangerous ones: unclassified means it may well be drawn,
    # and a check that passes because it could not tell is not a check.
    ok("and none of unknown destination", not other,
       "%d literals carry one and this cannot prove they are never drawn"
       % len(other))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("font 1 covers 0x20 to 0x7E. Anything else arrives as its UTF-8")
        print("bytes, one wrong glyph each. Use -, ->, ... and \" instead.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
