#!/usr/bin/env python3
"""BLE Spoofer's Spam All cycles every vendor, and only the real ones.

The Spoofer advertises one fixed device at a time. Spam All (deviceType 24)
rotates the payload through every real type on a timer so one run hits Apple,
Samsung, Google, Windows and Flipper targets together. The ways that breaks:

  1. The pseudo-type is wired: setAdvertisingData turns on the spam-all flag
     when deviceType == SPOOF_TYPE_ALL, and the device-type cycle wraps at
     SPOOF_TYPE_ALL so you can reach it.

  2. The loop actually rotates: when the flag is set it advances the tick and
     re-sets the advertisement, or the "all" option would just sit on one type.

  3. The rotation stays within the REAL types: (tick % SPOOF_TYPE_MAX) + 1, so
     it never applies type 24 to itself (which builds no packet) or type 0.

  4. It has a name on screen (spooferDeviceLabel case 24).

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

BT = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "bluetooth.cpp"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    src = BT.read_text(encoding="utf-8", errors="replace")
    flat = re.sub(r"\s+", " ", src)

    ok("SPOOF_TYPE_ALL / MAX defined",
       "SPOOF_TYPE_ALL" in src and "SPOOF_TYPE_MAX" in src)

    # 1. flag set on the pseudo-type; cycle reaches it.
    ok("setAdvertisingData arms spam-all on SPOOF_TYPE_ALL",
       re.search(r"s_spamAll\s*=\s*\(\s*deviceType\s*==\s*SPOOF_TYPE_ALL\s*\)", flat) is not None)
    ok("the device-type cycle wraps at SPOOF_TYPE_ALL",
       re.search(r"deviceType\s*>\s*SPOOF_TYPE_ALL", flat) is not None,
       "Spam All would be unreachable by cycling")

    # 2 + 3. loop rotates within real types and re-sets the advertisement.
    ok("loop advances within the real types only",
       re.search(r"s_spamTick\s*=\s*\(\s*s_spamTick\s*%\s*SPOOF_TYPE_MAX\s*\)\s*\+\s*1", flat) is not None,
       "a bad wrap could apply type 24 to itself or type 0")
    ok("loop re-applies the type and re-sets the advertisement",
       re.search(r"if\s*\(\s*s_spamAll\s*&&\s*isAdvertising[^}]*applySpoofType\s*\(\s*s_spamTick\s*\)[^}]*setAdvertisementData", flat) is not None,
       "the payload would never change")

    # 4. named on screen.
    ok("Spam All has a device label",
       re.search(r'case\s*24\s*:\s*return\s*"Spam All"', src) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
