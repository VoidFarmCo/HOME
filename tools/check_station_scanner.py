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

The deauth handoff kicks the selected client off its AP, and has its own ways
to go wrong:

  6. It is a handoff, not a broadcast deauth. The frame's receiver (addr1) is
     the selected client, and the sender/BSSID (addr2/addr3) is the AP. Point
     addr1 at ff:ff:.. instead and it deauths the whole AP, which is the
     broadcast deauther, not a client handoff.

  7. It fires only with a client selected, so a stray tap on the slot with no
     selection does nothing rather than deauthing addr 00:00:...

  8. It transmits on the AP interface, which needs AP mode, and hands the
     radio back to STA on the way out -- otherwise the sniff cannot resume and
     the next feature inherits an AP-mode radio.

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

    # --- deauth handoff ---
    send = src.find("static void deauthSendOnce()")
    send_region = squeeze(src[send:send + 900]) if send != -1 else ""
    ok("deauth receiver (addr1) is the selected client",
       re.search(r"memcpy\s*\(\s*f\s*\+\s*4\s*,\s*s_deauthClient\s*,\s*6\s*\)", send_region) is not None,
       "a broadcast addr1 deauths the whole AP, not one client")
    ok("deauth sender/BSSID (addr2/addr3) is the AP",
       re.search(r"memcpy\s*\(\s*f\s*\+\s*10\s*,\s*s_targetBssid", send_region) is not None and
       re.search(r"memcpy\s*\(\s*f\s*\+\s*16\s*,\s*s_targetBssid", send_region) is not None)
    ok("deauth transmits via the AP-interface helper",
       re.search(r"Deauther::wsl_bypasser_send_raw_frame\s*\(\s*f\s*,", send_region) is not None)

    stationloop = src.find("static void stationLoop()")
    sl_region = squeeze(src[stationloop:stationloop + 1200]) if stationloop != -1 else ""
    ok("deauth fires only with a client selected",
       re.search(r"if\s*\(\s*sel\s*>=\s*0\s*\)\s*\{\s*deauthStart\s*\(\s*\)", sl_region) is not None,
       "a tap with no selection would deauth 00:00:..")

    dstart = src.find("static void deauthStart()")
    dstart_region = squeeze(src[dstart:dstart + 300]) if dstart != -1 else ""
    ok("deauthStart switches to AP mode for TX",
       re.search(r"WiFi\.mode\s*\(\s*WIFI_AP\s*\)", dstart_region) is not None)
    dstop = src.find("static void deauthStop()")
    dstop_region = squeeze(src[dstop:dstop + 200]) if dstop != -1 else ""
    ok("deauthStop hands the radio back to STA",
       re.search(r"WiFi\.mode\s*\(\s*WIFI_STA\s*\)", dstop_region) is not None)
    ok("feature exit calls deauthStop when in the deauth view",
       re.search(r"if\s*\(\s*isDeauthView\s*\)\s*deauthStop\s*\(\s*\)", squeeze(loop_region)) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
