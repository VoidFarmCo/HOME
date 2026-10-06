#!/usr/bin/env python3
"""The chat compose keyboard fully initializes its config.

OnScreenKeyboardConfig has no default initializers (by design -- every caller
assigns field by name). showOnScreenKeyboard dereferences middleLabel,
emptyErrorMsg and the shuffle fields; leaving any of them indeterminate on the
stack is a crash (a garbage non-null pointer slips past the ?:/&& guards and
TFT_eSPI::textWidth faults). chat_core's compose() launches the keyboard for
every chat channel, so it MUST set the full set. Reads source; needs no board.

    python tools/check_chat_keyboard.py
"""
import re
import sys
from pathlib import Path

CORE = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "chat_core.cpp"
CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    src = CORE.read_text(encoding="utf-8", errors="replace")
    m = re.search(r"static void compose\(\)\s*\{(.*?)\n\}", src, re.S)
    ok("compose() found", m is not None)
    body = m.group(1) if m else ""

    # Every field showOnScreenKeyboard touches must be assigned in compose().
    for field in ("titleLine1", "titleLine2", "maxLen", "buttonsY",
                  "backLabel", "middleLabel", "okLabel",
                  "enableShuffle", "shuffleNames", "shuffleCount",
                  "requireNonEmpty", "emptyErrorMsg"):
        ok("compose sets cfg.%s" % field,
           re.search(r"cfg\.%s\s*=" % field, body) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
