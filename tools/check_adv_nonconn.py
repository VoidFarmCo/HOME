#!/usr/bin/env python3
"""A feature that only broadcasts is not connectable.

NimBLE defaults m_advParams.conn_mode to BLE_GAP_CONN_MODE_UND, undirected
connectable, whenever the peripheral role is compiled in. This firmware
compiles it in because the Rubber Ducky is a BLE HID device. It also puts the
device name in the advertisement.

So BleSpoofer, SourApple and AirTagSpoofer each advertised as a connectable
device named after the board, wearing somebody else's payload. A phone
answered the only part of that it understood and asked to pair with Pueo. It
kept asking after the feature was closed, because a central connecting and
disconnecting makes the host resume advertising, so stopping once on exit did
not settle it.

None of those three has anything to connect to. They are broadcasts.

    python tools/check_adv_nonconn.py

Reads source; needs no board.

What it asserts
---------------
Every NimBLEAdvertising::start() in a spoofing feature has a
setAdvertisementType(BLE_GAP_CONN_MODE_NON) before it, in the same function.

Ducky is exempt and must stay exempt: it is a keyboard, its whole purpose is
to be connected to, and a non-connectable HID device is a feature that cannot
work.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "ESP32-DIV"

# Namespaces that transmit someone else's identity and offer no service.
BROADCAST_ONLY = ("BleSpoofer", "SourApple", "AirTagSpoofer")

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


def strip_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


def enclosing_namespace(src, pos):
    best, name = -1, None
    for m in re.finditer(r"namespace\s+(\w+)\s*\{", src):
        if m.start() < pos and m.start() > best:
            best, name = m.start(), m.group(1)
    return name


def main():
    src = strip_comments(
        (SKETCH / "bluetooth.cpp").read_text(encoding="utf-8", errors="replace"))

    starts = [(m.start(), m.group(1)) for m in
              re.finditer(r"(\w+)->start\(\)\s*;", src)]
    # Only advertising objects, not scans.
    starts = [(p, v) for p, v in starts if "dvertis" in v.lower()]

    print("%d advertising starts in bluetooth.cpp" % len(starts))
    print()

    seen = set()
    for pos, var in starts:
        ns = enclosing_namespace(src, pos)
        ln = src[:pos].count("\n") + 1
        if ns not in BROADCAST_ONLY:
            print("    %-16s line %-5d not a broadcast-only feature, skipped"
                  % (ns, ln))
            continue
        seen.add(ns)
        # the mode must be set in the same function, before the start
        fn_start = src.rfind("\n}", 0, pos)
        window = src[max(0, fn_start):pos]
        ok("%s sets a non-connectable mode" % ns,
           "setAdvertisementType(BLE_GAP_CONN_MODE_NON)" in window,
           "line %d starts advertising without it" % ln)

    missing = [n for n in BROADCAST_ONLY if n not in seen]
    ok("every broadcast-only feature was found", not missing,
       "no advertising start seen in %s; renamed or removed?" % ", ".join(missing))

    # Ducky is a keyboard. It must stay connectable.
    ducky = strip_comments(
        (SKETCH / "ducky.cpp").read_text(encoding="utf-8", errors="replace"))
    ok("the Rubber Ducky is left connectable",
       "BLE_GAP_CONN_MODE_NON" not in ducky,
       "it is a HID device; non-connectable would stop it working")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("NimBLE advertises undirected-connectable by default and puts")
        print("the device name in the packet, so a spoof becomes a pairable")
        print("device called Pueo, and a disconnect restarts it.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
