#!/usr/bin/env python3
"""Every feature shows a way out, on the slot that actually is the way out.

featureExitButtonPressed() reads BTN_SELECT, which is the centre slot of the
touch nav bar. Almost every feature labels that slot "Exit". Three did not:
Spotter, Fast Pair and Hunt passed "" for the centre and put "Back" on the
left, where nothing reads it. On a touch-only board that is a feature you
can open and cannot leave -- the exit was there the whole time, on an
unlabelled button, next to a labelled one that did nothing.

It survived because those three were also the three that could not be
opened by touch at all (see check_menu_dispatch.py). Fixing that one
uncovered this one.

Two rules:

  * the centre slot is never an empty string. nullptr is fine -- it means
    "use the default icon for this slot", which is still something to press.
    "" is a blank button.

  * no slot is labelled with a word that means leaving when the feature
    routes leaving somewhere else. Specifically: "Back" or "Exit" on the
    left slot, while the centre is empty, is the exact shape of the bug.

Reads source. Does not need a board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

CALL = re.compile(r"setTouchNavLabels\(", re.S)
LEAVE_WORDS = {"back", "exit", "quit", "leave"}

# The wrapper inside utils.cpp and wifi.cpp forwards its own parameters;
# those are not label sites.
FORWARDER = re.compile(r"setTouchNavLabels\(left, down, center, up, right\)")


def split_args(text, start):
    depth = 0
    for i in range(start, len(text)):
        if text[i] in "([":
            depth += 1
        elif text[i] in ")]":
            depth -= 1
            if depth == 0:
                inner = text[start + 1:i]
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


def main():
    checks = 0
    failures = []

    for path in sorted(SKETCH.glob("*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        for m in CALL.finditer(src):
            if FORWARDER.match(src, m.start()):
                continue
            args = split_args(src, m.end() - 1)
            if args is None or len(args) != 5:
                continue
            line = src.count("\n", 0, m.start()) + 1
            where = f"{path.name}:{line}"
            left, _down, centre, _up, _right = args
            checks += 1

            cl = literal(centre)
            ll = literal(left)

            if cl == "":
                failures.append(
                    f"{where}: centre slot is \"\" -- that is the exit button, "
                    f"with no label on it")
            if ll is not None and ll.lower() in LEAVE_WORDS and cl == "":
                failures.append(
                    f"{where}: left says {ll!r} while the exit is the unlabelled centre")

    if not checks:
        print("no setTouchNavLabels call sites found; this check is testing nothing",
              file=sys.stderr)
        return 1

    for f in failures:
        print("  FAIL  " + f)
    if failures:
        print(f"\nFAILED: {len(failures)} of {checks} label sites")
        return 1

    print(f"  ok    {checks} nav label sites, every centre slot is pressable")
    print(f"\n{checks} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
