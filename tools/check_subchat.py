#!/usr/bin/env python3
"""SubGHz Chat talks only to H.O.M.E units, and cannot be overrun by a bad packet.

The CC1101 chat broadcasts text in packet mode at 433.92 MHz. The ways it breaks:

  1. It must be its own net: packet mode on (setCCMode 1), the H.O.M.E sync word
     (setSyncWord 0x48,0x4D) so only our units hear each other, and CRC on so
     corrupt frames are dropped in hardware rather than shown as garbage.

  2. A received packet is attacker-controlled. The parser reads a name length
     from byte 0 and must bound it (1..CHAT_NAME_MAX, and nameLen+1 <= len)
     before copying, or a crafted frame reads past the buffer.

  3. The chat is wired into the SubGHz menu dispatch.

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    src = (SKETCH / "SubChat.cpp").read_text(encoding="utf-8", errors="replace")
    flat = re.sub(r"\s+", " ", src)

    # 1. own net: packet mode, sync word, CRC.
    ok("packet mode is on (setCCMode(1))",
       re.search(r"setCCMode\s*\(\s*1\s*\)", flat) is not None)
    ok("the H.O.M.E sync word is set",
       re.search(r"setSyncWord\s*\(\s*0x48\s*,\s*0x4D\s*\)", flat, re.I) is not None,
       "without it the chat hears every 433 MHz packet")
    ok("CRC is on",
       re.search(r"setCrc\s*\(\s*1\s*\)", flat) is not None,
       "corrupt frames would show as garbage")

    # 2. RX length bounds before the copy.
    ok("RX bounds the name length from the packet",
       re.search(r"nameLen\s*<\s*1\s*\|\|\s*nameLen\s*>\s*CHAT_NAME_MAX\s*\|\|\s*nameLen\s*\+\s*1\s*>\s*len", flat) is not None,
       "a crafted packet could read past the buffer")
    ok("RX clamps the text length to the buffer",
       re.search(r"if\s*\(\s*textLen\s*>\s*CHAT_TEXT_MAX\s*\)\s*textLen\s*=\s*CHAT_TEXT_MAX", flat) is not None)

    # 3. menu dispatch.
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    ok("SubGHz Chat is dispatched from launchSubGhzFeature",
       re.search(r"SubChat::setup\s*,\s*SubChat::loop\s*,\s*SubChat::exit", ino) is not None)
    ok('"SubGHz Chat" is in the SubGHz menu',
       '"SubGHz Chat"' in ino)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
