#!/usr/bin/env python3
"""A custom chat name is stored, persists, and is what chat actually uses.

The chat handle defaults to the auto H-XXXX MAC name; a user-set name has to
(1) live in AppSettings, (2) be written by settingsSave and read by
settingsLoad or it resets every boot, (3) be editable from a Chat Name settings
row, and (4) actually be used by chat_core instead of the MAC name. Pin all
four. Reads source; needs no board.

    python tools/check_chat_name.py
"""
import re
import sys
from pathlib import Path

SK = Path(__file__).resolve().parent.parent / "ESP32-DIV"
CHECKS = 0
FAILED = []


def ok(name, cond):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name)
    if not cond:
        FAILED.append(name)


def main():
    store_h = (SK / "SettingsStore.h").read_text(encoding="utf-8", errors="replace")
    store_c = (SK / "SettingsStore.cpp").read_text(encoding="utf-8", errors="replace")
    utils = (SK / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    core = (SK / "chat_core.cpp").read_text(encoding="utf-8", errors="replace")

    ok("AppSettings has a chatName char field",
       re.search(r"char\s+chatName\s*\[", store_h) is not None)
    ok("settingsSave writes chatName", 'doc["chatName"]' in store_c and
       re.search(r'doc\["chatName"\]\s*=', store_c) is not None)
    ok("settingsLoad reads chatName",
       re.search(r's\.chatName|doc\["chatName"\]', store_c) is not None and
       store_c.count('doc["chatName"]') >= 2)

    ok("a Chat Name settings row exists (kChatNameRow)",
       re.search(r"\bkChatNameRow\b\s*=", utils) is not None)
    ok('the row is labelled "Chat Name"', '"Chat Name"' in utils)

    # chat_core uses the stored name, falling back to the MAC handle.
    dn = re.search(r"static void deriveName\(\)\s*\{(.*?)\n\}", core, re.S)
    ok("deriveName uses settings().chatName",
       dn is not None and "chatName" in dn.group(1))
    ok("deriveName still falls back to the H- MAC name",
       dn is not None and "H-%04X" in dn.group(1))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
