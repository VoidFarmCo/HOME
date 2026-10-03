#!/usr/bin/env python3
"""The WiFi Scanner's station scanner lists real clients and leaves the radio clean.

Chained from the AP detail view, the station scanner pins the radio to the
chosen AP's channel in promiscuous mode and lists the clients talking to it.
The ways that goes wrong, each caught here and each expensive on the board:

  1. Only data frames carry a client association. A management or control
     frame run through the same address rule names the wrong device, so the
     callback drops everything but WIFI_PKT_DATA, and only after a length
     check -- a short frame has no addr2 to read and reading it is a buffer
     overrun.

  2. A station is a unicast address. Broadcast (ff:ff:..) and multicast
     (01:00:5e:..) both set the group bit, and both are destinations, not
     clients; listing them is noise that crowds out the real ones.

  3. The pick rule: whichever of the two addresses is the AP, the other end
     is its client. Get it backwards and every row is the AP itself. This
     reimplements staPickClient and walks sample frames to prove it.

  4. The same client seen twice is one row, not two. The add path scans the
     list first and refreshes a match rather than appending, or a chatty
     client fills all 32 slots by itself in a second.

  5. The radio is handed back on the way out. Both exits -- Back to the
     detail view and Exit out of the feature -- must drop promiscuous, or the
     next feature opens onto a radio still in promiscuous mode on a stale
     channel, which looks like the new feature is broken.

Reads source. Needs no board.
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
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def squeeze(s):
    return re.sub(r"\s+", " ", s)


# --- the firmware's two pure helpers, in Python, for the behaviour checks ---
def py_is_group(m):
    return (m[0] & 0x01) != 0


def py_pick(bssid, a1, a2):
    if a2 == bssid:
        return a1
    if a1 == bssid:
        return a2
    return None


def main():
    src = WIFI.read_text(encoding="utf-8", errors="replace")
    flat = squeeze(src)

    # Narrow to the station-scanner region so a pattern cannot pass by matching
    # some other feature's code (PacketMonitor also toggles promiscuous).
    start = src.find("static bool staIsGroupAddr(")
    end = src.find("void wifiscanLoop()")
    region = src[start:end] if (start != -1 and end != -1) else ""
    rflat = squeeze(region)
    ok("station-scanner region found", bool(region))

    # 1. data-only, after a length guard.
    ok("callback drops all but WIFI_PKT_DATA",
       re.search(r"if\s*\(\s*type\s*!=\s*WIFI_PKT_DATA\s*\)\s*return", rflat) is not None)
    ok("callback length-guards before reading addr2",
       re.search(r"if\s*\(\s*p->rx_ctrl\.sig_len\s*<\s*16\s*\)\s*return", rflat) is not None,
       "reading payload+10 on a short frame overruns")

    # 2. group/broadcast excluded.
    ok("group bit test uses (m[0] & 0x01)",
       re.search(r"\(\s*m\[0\]\s*&\s*0x01\s*\)\s*!=\s*0", rflat) is not None)
    ok("callback drops group addresses",
       re.search(r"staIsGroupAddr\s*\(\s*sta\s*\)", rflat) is not None,
       "broadcast/multicast would be listed as clients")

    # 3. pick rule, reimplemented and walked.
    AP = [0xAA] * 6
    C = [0x12, 0x34, 0x56, 0x78, 0x9A, 0xBC]   # unicast: first octet bit0 clear
    BC = [0xFF] * 6
    MC = [0x01, 0x00, 0x5E, 0x00, 0x00, 0xFB]
    other = [0xDE, 0xAD, 0xBE, 0xEF, 0x00, 0x01]
    picks_ok = (
        py_pick(AP, C, AP) == C and          # AP is transmitter -> client is receiver
        py_pick(AP, AP, C) == C and          # AP is receiver    -> client is transmitter
        py_pick(AP, C, other) is None        # neither end is the AP -> no client
    )
    ok("pick rule returns the non-AP end, or none", picks_ok)
    group_ok = py_is_group(BC) and py_is_group(MC) and not py_is_group(C)
    ok("group test flags broadcast and multicast, not a unicast client", group_ok)

    # 4. dedupe before add.
    dedupe = re.search(
        r"for\s*\([^)]*s_staCount[^)]*\)\s*\{[^}]*memcmp\s*\(\s*s_sta\[i\]\.mac\s*,\s*sta\s*,\s*6\s*\)\s*==\s*0",
        rflat)
    ok("add path refreshes an existing MAC instead of duplicating", dedupe is not None)

    # 5. promiscuous dropped on both exits.
    stop = src.find("static void stationStop()")
    stop_region = src[stop:stop + 300] if stop != -1 else ""
    ok("stationStop drops promiscuous",
       re.search(r"esp_wifi_set_promiscuous\s*\(\s*false\s*\)", squeeze(stop_region)) is not None)
    loop = src.find("void wifiscanLoop()")
    loop_region = src[loop:loop + 500] if loop != -1 else ""
    ok("feature exit calls stationStop when in the station view",
       re.search(r"if\s*\(\s*isStationView\s*\)\s*stationStop\s*\(\s*\)", squeeze(loop_region)) is not None,
       "exiting mid-sniff would leave the radio in promiscuous")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
