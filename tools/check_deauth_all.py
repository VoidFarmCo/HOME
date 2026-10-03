#!/usr/bin/env python3
"""The Deauther's "Deauth All" hits every scanned AP, and is off by default.

The Deauther normally deauths the one selected AP. Deauth All (the Up slot on
the attack screen) cycles every AP in the scan instead, each on its own channel
-- Marauder's "deauth all". The ways that breaks:

  1. The send cycles the whole list: with the flag set it advances an index
     (idx % network_count) and sends to ap_list[idx] on that AP's channel,
     rather than always the one selectedAp -- or "all" would hit one AP.

  2. The Up slot toggles it on the attack screen.

  3. It is off at entry: deautherSetup resets the flag, so opening the Deauther
     never starts in all-APs mode by surprise.

Scoped to the Deauther namespace, since ProbeRequestFlood has a near-identical
attack screen. Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

WIFI = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "wifi.cpp"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    src = WIFI.read_text(encoding="utf-8", errors="replace")
    # Narrow to the Deauther feature namespace (the one with deautherSetup).
    start = src.find("namespace Deauther {", src.find("deauth_frame_default"))
    end = src.find("namespace ProbeRequestFlood", start)
    region = src[start:end] if (start != -1 and end != -1) else ""
    flat = re.sub(r"\s+", " ", region)
    ok("Deauther region found", bool(region))

    # 1. the send cycles ap_list on its own channel when s_deauthAll is set.
    ok("Deauth All cycles ap_list (idx %% network_count)",
       re.search(r"s_deauthAllIdx\s*=\s*\(\s*s_deauthAllIdx\s*\+\s*1\s*\)\s*%\s*network_count", flat) is not None,
       "it would not advance through the APs")
    ok("Deauth All sends to ap_list[idx] on its own channel",
       re.search(r"wsl_bypasser_send_deauth_frame\s*\(\s*&ap_list\[\s*s_deauthAllIdx\s*\]\s*,\s*ap_list\[\s*s_deauthAllIdx\s*\]\.primary", flat) is not None,
       "it would still hit only selectedAp")
    ok("the send is guarded by s_deauthAll",
       re.search(r"if\s*\(\s*s_deauthAll\s*&&\s*network_count\s*>\s*0", flat) is not None)

    # 2. toggled by the Up slot.
    ok("Up slot toggles s_deauthAll on the attack screen",
       re.search(r"isButtonPressedEdge\(BTN_UP\)[^;{}]*\{[^;]*s_deauthAll\s*=\s*!s_deauthAll", flat) is not None)

    # 3. off at entry.
    ok("deautherSetup resets s_deauthAll to false",
       re.search(r"s_deauthAll\s*=\s*false\s*;", flat) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
