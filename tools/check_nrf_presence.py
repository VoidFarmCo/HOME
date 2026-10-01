#!/usr/bin/env python3
"""Every nRF24 feature refuses when no nRF24 answers.

An absent module leaves MISO floating and the pull-up makes every register
read as 0xFF. That is not a neutral failure, it is a confident one.

STATUS reads 0xFF. Bit 6 is RX_DR, so "a packet is waiting" is permanently
true. The FIFO read then clocks out 32 bytes of 0xFF, esbTrimPayloadLen only
trims trailing zeros so it reports a full-length payload, and the ESB sniffer
writes it to the card. One row per millisecond, every row byte-for-byte
identical, no warning anywhere on screen. A capture that looks like data and
is a picture of an unconnected pin.

That shipped, and was found by reading a downloaded .jsonl rather than by
anything on the device. Scanner presented the same floating reads as channel
occupancy and MouseJack Scan presented them as devices. None of the six
features checked.

The probe was already written. Nrf24Raw::begin() puts 0x4C into RF_CH and
reads it back, and a register that returns what was just written is a chip
rather than a pull-up. Five of the six features never called it.

    python tools/check_nrf_presence.py

Reads source; needs no board.

Why this is a check and not a comment
-------------------------------------
check_stealth.py asserts the same shape for the same reason: a list of
features that honour a gate decays the moment somebody adds a seventh. The
failure mode here is worse than a jammer that keys under stealth, because
fabricated data does not announce itself. It goes in a file, gets
downloaded, and is read as a measurement.
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
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def func_body(src, signature):
    i = src.find(signature)
    if i < 0:
        return ""
    j = src.index("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j:k + 1]
    return ""


# The six, by the setup() both dispatch chains reach and the label its
# refusal screen shows. The label is checked too: a screen naming some other
# feature is a screen nobody believes.
#
# Three of them transmit and so already refuse under Stealth Mode. That gate
# asks about intent and comes first; this one asks about the board.
FEATURES = [
    ("scannerSetup",         "Scanner",          False),
    ("prokillSetup",         "Proto Kill",       True),
    ("esbSnifferSetup",      "ESB Sniffer",      False),
    ("esbReplaySetup",       "ESB Replay",       True),
    ("mouseJackSetup",       "MouseJack Scan",   False),
    ("mouseJackInjectSetup", "MouseJack Inject", True),
]


def main():
    src = (SKETCH / "bluetooth.cpp").read_text(encoding="utf-8",
                                               errors="replace")
    raw = (SKETCH / "Nrf24Raw.cpp").read_text(encoding="utf-8",
                                              errors="replace")

    print("the probe writes and reads back, rather than reading and hoping:")
    begin = func_body(raw, "bool begin()")
    ok("Nrf24Raw::begin() found", bool(begin))
    # A read-only probe cannot tell a chip from a pull-up: 0xFF is a legal
    # value for most of these registers. Writing a known byte and getting it
    # back is the only answer a floating line cannot give.
    m = re.search(r"writeReg\(REG_RF_CH,\s*(0x[0-9A-Fa-f]+)\)", begin)
    ok("it writes a known value to RF_CH", m is not None)
    if m:
        val = m.group(1)
        ok("  and compares the read-back against that same value",
           re.search(r"readReg\(REG_RF_CH\)\s*==\s*" + val, begin) is not None,
           "the probe writes %s and checks something else" % val)
        ok("  and the value is not a floating-bus value",
           int(val, 16) not in (0x00, 0xFF),
           "%s is what an absent module reads" % val)

    print("\nevery feature that touches the radio asks first:")
    for fn, label, stealth in FEATURES:
        body = func_body(src, "void %s()" % fn)
        ok("%-22s (%s)" % (label, fn),
           bool(body) and 'nrfReady("' in body,
           "no body found for %s" % fn if not body
           else "reads the registers without checking a chip is there")
        ok("  and names itself", bool(body) and ('nrfReady("%s")' % label) in body,
           "the refusal screen names some other feature")
        # Return on refusal, not continue. nrfReady sets
        # feature_exit_requested, but the setup it was called from runs to
        # completion first, and what that completion does is configure a
        # radio that is not there and start a poll loop against 0xFF.
        if body:
            ok("  and returns when it refuses",
               re.search(r'if \(!nrfReady\("%s"\)\) return;' % re.escape(label),
                         body) is not None,
               "the result is read but the setup carries on anyway")
        if stealth:
            # Order matters only in one direction: a transmitter asked to
            # transmit under stealth should be told about stealth, which is
            # about what it would do, rather than about a module, which is
            # about what it is plugged into.
            s_at = body.find("Stealth::refuse")
            n_at = body.find("nrfReady")
            ok("  and stealth is asked before the hardware",
               0 <= s_at < n_at,
               "the hardware gate comes first, so stealth is never reached")

    print("\nthe refusal screen says what to do about it:")
    miss = func_body(src, "static void nrfReportMissing(const char* feature)")
    ok("nrfReportMissing() found", bool(miss))
    ok("it names the chip", '"No nRF24"' in miss)
    ok("it names the pins to check",
       all(p in miss for p in ("MISO", "CSN", "CE", "SCK", "MOSI")),
       "a refusal that does not say where to look is a device that "
       "appears broken")

    ready = func_body(src, "static bool nrfReady(const char* feature)")
    ok("nrfReady() waits for the user",
       "feature_exit_requested = true" in ready and "for (;;)" in ready,
       "a message drawn and then dropped is on screen for one frame")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
