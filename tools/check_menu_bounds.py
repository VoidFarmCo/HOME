#!/usr/bin/env python3
"""displayMenu() never repaints the old menu from the tile home, and never
indexes menu_items out of bounds.

Two bugs in one: toolRun() sets current_menu_index to the tool CATEGORY (Tools =
7), but menu_items[] has 7 entries (0..6) after RFID removal, so the old
displayMenu() path did menu_items[7] -> a bad char* -> TFT_eSPI::textWidth
crash/reboot; and it repainted the retired radio menu on feature exit ("old
screen popping up"). displayMenu() must bail when a tool was launched from the
purpose-tile home, and clamp the index for any other caller. Reads source.

    python tools/check_menu_bounds.py
"""
import re, sys
from pathlib import Path

INO = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "ESP32-DIV.ino"
CHECKS = 0; FAILED = []
def ok(n, c):
    global CHECKS; CHECKS += 1
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)

def main():
    src = INO.read_text(encoding="utf-8", errors="replace")
    body = re.search(r"void displayMenu\(\)\s*\{(.*?)\n\}", src, re.S)
    b = body.group(1)[:600] if body else ""
    ok("displayMenu() found", bool(body))
    ok("displayMenu bails when launched from the tile home",
       re.search(r"if\s*\(\s*g_toolFromHome\s*\|\|\s*in_playbook_home\s*\)\s*return", b) is not None)
    ok("displayMenu clamps current_menu_index to menu_items bounds",
       re.search(r"current_menu_index\s*>=\s*NUM_MENU_ITEMS", b) is not None
       and "current_menu_index = 0" in b)
    ok("no raw menu_items[current_menu_index] (all via safeMenuIdx)",
       "menu_items[current_menu_index]" not in src)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS)); return 1
    print("%d checks passed" % CHECKS); return 0

if __name__ == "__main__":
    sys.exit(main())
