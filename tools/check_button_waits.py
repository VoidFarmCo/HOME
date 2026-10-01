#!/usr/bin/env python3
"""No button wait spins without yielding.

while (isButtonPressed(b)) {} was in 55 places. It is the obvious way to wait
for a release and it is a trap: the loop never yields, so a button that does
not read as released takes the device with it. No repaint, no input, nothing
feeding the watchdog, and from the outside it is indistinguishable from a
crash.

A physically stuck button is rare, which is why this survived. The path that
makes it worth fixing is the touch controller: isButtonPressed falls through to
the touch nav when no physical button is down, and a touch read that reports a
press it never clears wedges the loop. This project has already watched touch
lose the shared SPI bus once.

waitForButtonRelease() replaced them. It yields through delay() and gives up
after a timeout, so the worst case is a wait rather than a hang.

    python tools/check_button_waits.py

Reads source; needs no board.

What is still allowed
---------------------
A while-loop on isButtonPressed whose body yields. Four of those remain, all
waiting on two buttons at once, which the one-button helper does not express.
They are safe because they yield, and that is what this asserts: not the shape
of the loop, but that it cannot spin.
"""
import glob
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(os.path.dirname(HERE), "ESP32-DIV")

WHILE = re.compile(r"^\s*while\s*\(.*isButtonPressed.*\)\s*\{\s*$")
YIELDS = re.compile(r"\b(delay|vTaskDelay|yield)\s*\(")
HELPER = re.compile(r"bool\s+waitForButtonRelease\s*\(\s*int\s+\w+\s*,\s*uint32_t\s+\w+\s*\)")


def main():
    problems = []
    spinless = 0
    calls = 0

    for path in sorted(glob.glob(os.path.join(SKETCH, "*.cpp"))
                       + glob.glob(os.path.join(SKETCH, "*.ino"))):
        name = os.path.basename(path)
        lines = open(path, encoding="utf-8", errors="replace").read().splitlines()
        calls += sum(1 for l in lines if "waitForButtonRelease(" in l
                     and "bool waitForButtonRelease" not in l)

        for i, line in enumerate(lines):
            if not WHILE.match(line):
                continue
            # Brace depth, not the first closing brace on its own line: the
            # helper's own loop has an if inside it, and stopping at that
            # inner brace hid the delay() underneath and reported it as a spin.
            body = []
            depth = 1
            j = i + 1
            while j < len(lines) and depth > 0:
                depth += lines[j].count("{") - lines[j].count("}")
                if depth > 0:
                    body.append(lines[j])
                j += 1
            if not any(YIELDS.search(b) for b in body):
                problems.append("%s:%d waits on a button without yielding: %s"
                                % (name, i + 1, line.strip()))
            else:
                spinless += 1

    src = ""
    for f in ("ESP32-DIV.ino",):
        src += open(os.path.join(SKETCH, f), encoding="utf-8",
                    errors="replace").read()
    m = HELPER.search(src)
    if not m:
        problems.append("waitForButtonRelease(int, uint32_t) is not defined; "
                        "the calls below have nothing to call")
    else:
        # Its own body, not the whole file. Looking for an elapsed-time test
        # anywhere in ESP32-DIV.ino passes on somebody else's timer, which is
        # how a mutation that gutted this very function went unnoticed.
        body, depth, started = [], 0, False
        for ch in src[m.start():]:
            if ch == "{":
                depth += 1
                started = True
            elif ch == "}":
                depth -= 1
            body.append(ch)
            if started and depth == 0:
                break
        body = "".join(body)
        if "timeoutMs" not in body or ">=" not in body:
            problems.append("waitForButtonRelease has no elapsed-time test in "
                            "its own body; a wait that cannot give up is the "
                            "bug it replaced")

    if problems:
        for p in problems:
            print("  FAIL  " + p)
        print()
        print("FAILED: %d" % len(problems))
        return 1

    print("  ok    %d calls to waitForButtonRelease, %d remaining loops all "
          "yield, none spin" % (calls, spinless))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
