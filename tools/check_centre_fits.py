#!/usr/bin/env python3
"""Centred text drawn straight through drawCentreString, in any font.

check_text_fits.py measures the centred lines that go through showLine().
That is not all of them. 35 call sites hand text to tft.drawCentreString()
directly, and nothing measured any of them, because showLine is the only
name that check matches and the About page is in a .ino file it does not
even open.

That gap is how the About page came to be measured by hand. It draws its
tagline and byline in font 4, and the only way to know whether a 26 px
proportional face fits 320 px is to add up the glyphs: a 24 character line
is anywhere from 24 to 600 px in that font. tft_fonts.py carries the tables,
read from the TFT_eSPI zip that ships in the release archive.

Centred, not left aligned, so a line that is too wide loses both ends and
what survives is the middle. TFT_eSPI does not clip or wrap.

    python tools/check_centre_fits.py

Reads source; needs no board.

One panel
---------
320 px, read from shared.h. The older checks carry a PANELS list with a
240 px "2.8 inch" entry beside it; that panel was dropped and shared.h
defines 320x480 unconditionally, so measuring against 240 here would fail a
release over a board that cannot be built.

What it skips, and says so
--------------------------
Text it cannot resolve to a literal: a String expression, a .c_str(), or a
parameter whose callers are not traced. A font outside 1, 2 and 4. A centre
x that does not resolve. Each skip is counted and the counts print on every
run, because a check that measures four things out of thirty-five while
printing "ok" is worse than no check.
"""
import re
import sys
from pathlib import Path

import tft_fonts
from check_text_pitch import calls, functions

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

NUM = re.compile(r"^-?\d+$")
STR_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')
TOKEN = re.compile(r'"(?:[^"\\]|\\.)*"|[A-Za-z_]\w*|\S')

# Expressions that are the middle of the panel, however they are spelled.
CENTRE = (
    "DISPLAY_WIDTH/2", "DISPLAY_WIDTH / 2",
    "PUEO_SCREEN_W/2", "PUEO_SCREEN_W / 2",
    "tft.width()/2", "tft.width() / 2",
)


def panel_width():
    src = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    m = re.search(r"^#define\s+PUEO_SCREEN_W\s+(\d+)\s*$", src, re.M)
    if not m:
        raise SystemExit("FAIL: shared.h no longer defines PUEO_SCREEN_W, so\n"
                         "      this does not know how wide the panel is.")
    return int(m.group(1))


def string_defines():
    """#define NAME "text", over the headers, resolved through each other.

    PUEO_TAGLINE is a literal; the About byline is four tokens of which two
    are macros. Both have to come out as the characters the panel draws.
    """
    out = {}
    pat = re.compile(r'^#define\s+(\w+)\s+((?:\s*"(?:[^"\\]|\\.)*")+)\s*$',
                     re.M)
    for path in sorted(SKETCH.glob("*.h")):
        src = path.read_text(encoding="utf-8", errors="replace")
        for m in pat.finditer(src):
            out[m.group(1)] = "".join(
                s for s in STR_LITERAL.findall(m.group(2)))
    return out


def unescape(s):
    return (s.replace('\\"', '"').replace("\\\\", "\\")
             .replace("\\n", "\n").replace("\\t", "\t"))


def resolve_text(expr, defines, depth=0):
    """The characters an argument draws, or None.

    C adjacent-literal concatenation, with macros standing in for literals.
    Every token has to resolve: one that does not means the width is not in
    the source, and a partial answer here would be a smaller number than the
    truth, which is the direction that passes a line that overruns.
    """
    expr = expr.strip()
    if not expr or depth > 4:
        return None
    toks = TOKEN.findall(expr)
    parts = []
    for t in toks:
        if t.startswith('"'):
            parts.append(unescape(t[1:-1]))
        elif t in defines:
            inner = defines[t]
            parts.append(inner)
        else:
            return None
    if not parts:
        return None
    return "".join(parts)


def main():
    width = panel_width()
    defines = string_defines()

    problems = []
    measured = 0
    skipped = {}

    def skip(why):
        skipped[why] = skipped.get(why, 0) + 1

    files = sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.ino"))
    seen_any = False
    for path in files:
        src = path.read_text(encoding="utf-8", errors="replace")
        if "drawCentreString" not in src:
            continue
        for fname, line0, body in functions(src):
            for off, args in calls(body, "drawCentreString"):
                if len(args) < 3:
                    continue
                seen_any = True
                line = line0 + body[:off].count("\n")
                where = "%s:%s()" % (path.name, fname)

                if len(args) < 4 or not NUM.match(args[3].strip()):
                    skip("the font is not a literal in the call")
                    continue
                font = int(args[3].strip())
                if not tft_fonts.known(font):
                    skip("font %d, which has no width table" % font)
                    continue

                cx = args[1].strip()
                if cx.replace(" ", "") not in [c.replace(" ", "")
                                               for c in CENTRE]:
                    if NUM.match(cx):
                        centre = int(cx)
                    else:
                        skip("a centre x that does not resolve")
                        continue
                else:
                    centre = width // 2

                text = resolve_text(args[0], defines)
                if text is None:
                    skip("text this cannot resolve to a literal")
                    continue

                px = tft_fonts.width(text, font)
                if px is None:
                    skip("a character outside the font")
                    continue

                measured += 1
                left = centre - px // 2
                right = left + px
                if left < 0 or right > width:
                    problems.append((where, line, font, text, px,
                                     left, right))

    if not seen_any:
        print("FAIL: no drawCentreString calls matched. The call scanner or")
        print("      the function scanner stopped working, and this is")
        print("      reporting green over nothing.")
        return 1

    print("centred strings measured: %d of %d on a %d px panel"
          % (measured, measured + sum(skipped.values()), width))
    for why, n in sorted(skipped.items(), key=lambda kv: -kv[1]):
        print("  skipped %4d  %s" % (n, why))
    print()

    if not problems:
        print("every one of them fits the panel it is centred on.")
        return 0

    for where, line, font, text, px, left, right in problems:
        print("  %-34s line %-5d font %d  %d px, x %d..%d"
              % (where, line, font, px, left, right))
        print("  %-34s \"%s\"" % ("", text))
    print()
    print("FAILED: %d centred line(s) run off the panel." % len(problems))
    print()
    print("TFT_eSPI does not clip or wrap. A centred line that is too wide")
    print("loses both ends, so what is left on screen is the middle of the")
    print("message. Shorten it, split it, or draw it in a smaller font.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
