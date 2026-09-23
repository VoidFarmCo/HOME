#!/usr/bin/env python3
"""A bigger font with the old line pitch left under it.

The list screens draw in PUEO_BODY_FONT, which is font 2 on the 3.5" panel
and font 1 on the 2.8". Font 2 glyphs are 16 px tall; font 1's are 8. So a
vertical measurement written as a literal cannot be right for both panels,
and on the 3.5" it is wrong in the worst way: the lines land on top of each
other and the band that is supposed to erase the old text leaves the bottom
half of it behind.

This is not a hypothetical. It happened four times on the way in --
Fast Pair's confirmation page, its result page, Hunt's gauge readout, and
Fast Pair's device list, which is the screen the feature is for. They were
found one at a time, by reading the file, reading further, somebody looking
at the board, and finally by writing this. Nothing connected them, and they
were all the same mistake.

The rule is that inside a function which sets PUEO_BODY_FONT, a vertical
measurement has to be a named constant:

  * `y += 11`                        -- a pitch. PUEO_BODY_LINE or _GAP.
  * `uiShowLine(..., y + 21, ...)`   -- an offset from a row top. Same.
  * `uiShowLine(..., 11, ...)`       -- the height of the band it clears,
                                        which must cover the glyph.

An absolute position (`8, 24`) is left alone: that is where a thing is, not
how far apart two things are, and it does not scale with the font. So is a
small offset inside a band -- `top + 6` is whitespace, not a line pitch.
The cut is at 10 px, below the smaller font's own 11 px pitch.

    python tools/check_text_pitch.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

# Below the smaller panel's own 11 px line pitch. A literal under this is
# space inside a line; at or above it, it is a distance between lines.
PITCH_FLOOR = 10

# TFT_eSPI font 2. A band shorter than this does not erase the glyph.
FONT2_H = 16


def functions(src):
    """(name, start_line, text) for each top-level function body.

    Brace counting, not a parser. It only has to find where one function
    ends and the next begins, and the firmware is plain C++ laid out in the
    usual way: a signature in column 1, a body, a closing brace in column 1.
    """
    out = []
    lines = src.splitlines()
    i = 0
    while i < len(lines):
        m = re.match(r"^[A-Za-z_][\w:<>,\* &]*\s[\*&]?(\w+)\s*\(", lines[i])
        if not m:
            i += 1
            continue
        start = i
        depth = 0
        seen = False
        while i < len(lines):
            depth += lines[i].count("{") - lines[i].count("}")
            if "{" in lines[i]:
                seen = True
            i += 1
            if seen and depth <= 0:
                break
        if seen:
            out.append((m.group(1), start + 1, "\n".join(lines[start:i])))
    return out


def split_args(s):
    """Top-level commas only.

    Arguments hold calls, which hold commas -- and they hold strings, which
    also hold commas. Missing the second one is not a false positive, it is
    a silent skip: `drawString("Find My, Tile, SmartTag", 8, top + 38)`
    parsed as six arguments put a fragment of the caption where the y
    belongs and the line was never looked at. Quotes are tracked.
    """
    args, depth, cur = [], 0, ""
    quote, esc = None, False
    for ch in s:
        if quote:
            cur += ch
            if esc:
                esc = False
            elif ch == "\\":
                esc = True
            elif ch == quote:
                quote = None
            continue
        if ch in "\"'":
            quote = ch
            cur += ch
            continue
        if ch in "([":
            depth += 1
        elif ch in ")]":
            depth -= 1
        if ch == "," and depth == 0:
            args.append(cur.strip())
            cur = ""
        else:
            cur += ch
    args.append(cur.strip())
    return args


def calls(text, name):
    """Every `name(...)` in text, as (offset, [args]). Spans line breaks."""
    out = []
    for m in re.finditer(r"\b%s\s*\(" % name, text):
        i = m.end()
        depth = 1
        quote, esc = None, False
        while i < len(text) and depth:
            ch = text[i]
            if quote:
                if esc:
                    esc = False
                elif ch == "\\":
                    esc = True
                elif ch == quote:
                    quote = None
            elif ch in "\"'":
                quote = ch
            elif ch in "([":
                depth += 1
            elif ch in ")]":
                depth -= 1
            i += 1
        out.append((m.start(), split_args(text[m.end():i - 1])))
    return out


def trailing_literal(expr):
    """`y + 21` -> 21. `40 + PUEO_BODY_LINE` -> None. `y` -> None.

    Only a literal in the last position is an offset from a base. A literal
    that is itself the base, with a named constant added to it, is already
    following the font.
    """
    m = re.search(r"\+\s*(\d+)\s*$", expr.strip())
    return int(m.group(1)) if m else None


def bare_literal(expr):
    return int(expr) if re.fullmatch(r"\d+", expr.strip()) else None


def main():
    problems = []
    blind = []
    checked = 0

    SETS = "setTextFont(PUEO_BODY_FONT)"

    for path in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino")):
        src = path.read_text(encoding="utf-8", errors="replace")
        if "PUEO_BODY_FONT" not in src:
            continue

        # Every place the font is set has to land inside a function this
        # found, or the lines under it were never looked at. functions() is
        # brace counting against a signature in column 1, and a signature it
        # stops recognising would take a whole screen out of scope silently.
        want = src.count(SETS)
        got = sum(b.count(SETS) for _, _, b in functions(src))
        if got != want:
            blind.append((path.name, want, got))

        for fname, line0, body in functions(src):
            if "setTextFont(PUEO_BODY_FONT)" not in body:
                continue
            checked += 1
            where = "%s:%s()" % (path.name, fname)

            def at(off):
                return line0 + body[:off].count("\n")

            # A pitch written as a literal.
            for m in re.finditer(r"\by\s*\+=\s*(\d+)\s*;", body):
                if int(m.group(1)) >= PITCH_FLOOR:
                    problems.append(
                        (where, at(m.start()),
                         "y += %s -- a line pitch as a literal; the 3.5\" "
                         "font is 16 px" % m.group(1)))

            # uiShowLine(dst, cap, text, x, y, h, fg, bg)
            for off, args in calls(body, "uiShowLine"):
                if len(args) < 6:
                    continue
                y, h = args[4], args[5]
                lit = trailing_literal(y)
                if lit is not None and lit >= PITCH_FLOOR:
                    problems.append(
                        (where, at(off),
                         "y offset +%d as a literal in %s" % (lit, " ".join(
                             y.split()))))
                lit = bare_literal(h)
                if lit is not None and lit < FONT2_H:
                    problems.append(
                        (where, at(off),
                         "clears a %d px band under a 16 px glyph" % lit))

            # drawString(text, x, y[, font])
            for off, args in calls(body, "drawString"):
                if len(args) < 3:
                    continue
                lit = trailing_literal(args[2])
                if lit is not None and lit >= PITCH_FLOOR:
                    problems.append(
                        (where, at(off),
                         "y offset +%d as a literal in %s"
                         % (lit, " ".join(args[2].split()))))

    print("functions drawing in PUEO_BODY_FONT: %d" % checked)
    if checked == 0 or blind:
        print()
        for name, want, got in blind:
            print("  %s sets the body font %d time(s); %d landed inside a"
                  " function this could see" % (name, want, got))
        print("FAIL: this check is not looking at what it claims to. Either")
        print("      the constant was renamed or the function scanner stopped")
        print("      matching a signature -- a screen has left its scope and")
        print("      it would go on reporting green.")
        return 1

    print()
    if not problems:
        print("every vertical measurement in them follows the font.")
        return 0

    for where, line, what in problems:
        print("  %-34s line %-5d %s" % (where, line, what))
    print()
    print("FAILED: %d vertical measurement(s) written as literals." % len(
        problems))
    print()
    print("On the 3.5\" panel these draw a 16 px glyph on a font-1 advance:")
    print("the lines overlap, and the band that clears the old text leaves")
    print("the bottom of it on screen. Use PUEO_BODY_LINE, PUEO_BODY_GAP or")
    print("PUEO_BODY_H from shared.h.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
