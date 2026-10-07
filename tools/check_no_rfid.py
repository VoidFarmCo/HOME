#!/usr/bin/env python3
"""RFID/NFC stays removed.

The PN532 RFID/NFC feature was dropped: it is not stocked, off-core, and its
2.4 KB of DRAM and a menu slot are wanted elsewhere. This pins the removal so a
later merge or copy-paste cannot quietly bring it back -- no feature file, no
menu entry, no PN532 pin wiring, no launcher. Reads source; needs no board.

    python tools/check_no_rfid.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SK = ROOT / "ESP32-DIV"
CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    ino = (SK / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")

    ok("rfid.cpp is gone", not (SK / "rfid.cpp").exists())
    ok("rfid.h is gone", not (SK / "rfid.h").exists())

    m = re.search(r"NUM_MENU_ITEMS\s*=\s*(\d+)", ino)
    ok("NUM_MENU_ITEMS == 7 (RFID slot removed)",
       m is not None and int(m.group(1)) == 7)
    ok('no "RFID/NFC" menu entry', '"RFID/NFC"' not in ino)
    ok("no RfidNfc references", "RfidNfc" not in ino)
    ok("no launchRfidFeature", "launchRfidFeature" not in ino)
    ok("no rfid submenu table", "rfid_submenu_items" not in ino)

    # No board actively wires a PN532 (board_pueo.h is upstream's and ignored).
    bh = (SK / "board_home.h").read_text(encoding="utf-8", errors="replace")
    sh = (SK / "shared.h").read_text(encoding="utf-8", errors="replace")
    ok("board_home.h defines no PN532 pin", "PN532" not in bh)
    ok("shared.h defines no PN532 pin defaults",
       re.search(r"#define\s+PN532_", sh) is None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
