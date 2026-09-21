#!/usr/bin/env python3
"""Every feature shows a way out, on the slot that actually is the way out.

featureExitButtonPressed() reads BTN_SELECT, which is the centre slot of the
touch nav bar. Almost every feature labels that slot "Exit". Three did not:
Spotter, Fast Pair and Hunt passed "" for the centre and put "Back" on the
left, where nothing reads it. On a touch-only board that is a feature you
can open and cannot leave -- the exit was there the whole time, on an
unlabelled button, next to a labelled one that did nothing.

That hid behind check_menu_dispatch.py's bug: those same three could not be
opened by touch at all, so nobody got far enough in to find they could not
get out.

Then the labels were right and still invisible. setTouchNavLabels() only
stores them; redrawTouchButtonBar() paints them. Spotter and Fast Pair did
call it -- and then called redraw(true), whose first act is fillScreen. The
bar was drawn and wiped inside one setup, which from the outside looks
exactly like never drawing it.

Four rules, one per way this has actually broken:

  1. the centre slot is never an empty string. nullptr is fine: it draws the
     default icon, which is still something to press. "" is a blank button.

  2. no slot promises a way out the feature does not route there -- "Back"
     on the left while the centre is blank is the same bug as rule 1, named
     separately so the report says why it matters.

  3. a file that sets labels also repaints the bar. File-level rather than
     per-call: several features set labels in a helper and repaint in the
     caller, and a line-window rule would fail those for no reason.

  4. nothing clears the screen between the repaint and the end of its
     function. A clear is tft.fillScreen(), or a call to redraw() -- this
     tree's idiom for a full repaint, and the one that hid the bug, because
     the call site says redraw(true) and never says fillScreen.

Reads source. Does not need a board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

SET_LABELS = re.compile(r"\bsetTouchNavLabels\s*\(")
REPAINT = re.compile(r"\bredrawTouchButtonBar\s*\(\s*\)")
CLEAR = re.compile(r"\btft\.fillScreen\s*\(|\bredraw\s*\(")

# utils.cpp and wifi.cpp each wrap the setter and forward their own
# parameters. Those are not label sites.
FORWARDER = re.compile(r"\bsetTouchNavLabels\(left, down, center, up, right\)")

LEAVE_WORDS = {"back", "exit", "quit", "leave"}


def split_args(text, open_paren):
    """The arguments of the call whose '(' is at open_paren, or None."""
    depth = 0
    for i in range(open_paren, len(text)):
        if text[i] in "([":
            depth += 1
        elif text[i] in ")]":
            depth -= 1
            if depth == 0:
                inner = text[open_paren + 1:i]
                out, buf, d = [], "", 0
                for ch in inner:
                    if ch in "([":
                        d += 1
                    elif ch in ")]":
                        d -= 1
                    if ch == "," and d == 0:
                        out.append(buf.strip())
                        buf = ""
                    else:
                        buf += ch
                out.append(buf.strip())
                return out
    return None


def literal(arg):
    """The string a slot shows, or None when it is not a plain literal."""
    m = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', arg)
    return m.group(1) if m else None


def line_of(src, pos):
    return src[:pos].count("\n") + 1


def main():
    sources = sorted(SKETCH.glob("*.cpp"))
    if not sources:
        print("no sources under %s" % SKETCH, file=sys.stderr)
        return 1

    checks = 0
    failures = []

    for path in sources:
        src = path.read_text(encoding="utf-8", errors="replace")

        # Rules 1 and 2: what each slot says.
        for m in SET_LABELS.finditer(src):
            if FORWARDER.match(src, m.start()):
                continue
            args = split_args(src, m.end() - 1)
            if args is None or len(args) != 5:
                continue
            checks += 1
            where = "%s:%d" % (path.name, line_of(src, m.start()))
            centre = literal(args[2])
            left = literal(args[0])
            if centre == "":
                failures.append(
                    '%s: centre slot is "" -- that is the exit button, with '
                    "no label on it" % where)
            if left is not None and left.lower() in LEAVE_WORDS and centre == "":
                failures.append(
                    "%s: left says %r while the exit is the unlabelled centre"
                    % (where, left))

        # Rule 3: whoever sets labels paints them somewhere.
        sets = [m for m in SET_LABELS.finditer(src)
                if not FORWARDER.match(src, m.start())]
        if sets:
            checks += 1
            if not REPAINT.search(src):
                failures.append(
                    "%s: sets nav labels %dx and never calls "
                    "redrawTouchButtonBar() -- the bar keeps the menu's icons"
                    % (path.name, len(sets)))

        # Rule 4: the repaint is not undone before the function returns.
        # Scope runs from the repaint to the next line closing at column 0,
        # which in this tree is the end of the enclosing function.
        for m in REPAINT.finditer(src):
            checks += 1
            close = src.find("\n}", m.end())
            span = src[m.end():close if close != -1 else len(src)]
            cm = CLEAR.search(span)
            if cm:
                failures.append(
                    "%s:%d: the nav bar is repainted and then %s() clears it "
                    "again before the function returns"
                    % (path.name, line_of(src, m.start()),
                       cm.group(0).rstrip("( ").strip()))

    for f in failures:
        print("  FAIL  " + f)

    if failures:
        print("\nFAILED: %d of %d" % (len(failures), checks))
        return 1

    print("  ok    every centre slot is pressable, painted, and not cleared "
          "afterwards")
    print("\n%d checks passed" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
