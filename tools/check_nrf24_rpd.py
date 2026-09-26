#!/usr/bin/env python3
"""The RPD sampling path, checked against the nRF24L01+ datasheet.

Three of these are register facts that a typo would silently invert, and the
fourth is the one that actually bites.

RPD latches. It is set when the receiver sees more than about -64 dBm and it
is only cleared by leaving RX mode. A sweep that raises CE once and then
walks the channels reading RPD gets the right answer for the first busy
channel and that same answer for every channel after it, and reports a fully
occupied band. Nothing about that looks like a bug on screen: a jammer test
would even seem to confirm it.

So the sequence is asserted here rather than left to a comment: CE has to go
down after every read.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
CPP = ROOT / "ESP32-DIV" / "Nrf24Raw.cpp"
HDR = ROOT / "ESP32-DIV" / "Nrf24Raw.h"

cpp = CPP.read_text(encoding="utf-8", errors="replace")
hdr = HDR.read_text(encoding="utf-8", errors="replace")

checks = 0
failed = []


def case(name, cond, detail=""):
    global checks
    if cond:
        checks += 1
        print("  ok  %-38s %s" % (name, detail))
    else:
        failed.append(name)
        print("  FAIL %-38s %s" % (name, detail))


def const(text, name):
    m = re.search(r"constexpr\s+uint8_t\s+%s\s*=\s*(0x[0-9A-Fa-f]+|\d+)" % name, text)
    return int(m.group(1), 0) if m else None


def const_hdr(name):
    m = re.search(r"constexpr\s+uint8_t\s+%s\s*=\s*(\d+)" % name, hdr)
    return int(m.group(1)) if m else None


# ── register facts, datasheet section 9 ──────────────────────────────────
case("REG_RPD is 0x09", const(cpp, "REG_RPD") == 0x09,
     "got %s" % const(cpp, "REG_RPD"))
case("RPD is bit 0", const(cpp, "RPD_SIGNAL") == 0x01,
     "got %s" % const(cpp, "RPD_SIGNAL"))
case("CONFIG PRIM_RX is bit 0", const(cpp, "CFG_PRIM_RX") == 0x01,
     "got %s" % const(cpp, "CFG_PRIM_RX"))
case("CONFIG PWR_UP is bit 1", const(cpp, "CFG_PWR_UP") == 0x02,
     "got %s" % const(cpp, "CFG_PWR_UP"))

# ── the band ──────────────────────────────────────────────────────────────
lo, hi = const_hdr("kChanMin"), const_hdr("kChanMax")
case("kChanMax stays inside ISM", hi is not None and 2400 + hi <= 2483.5,
     "%s -> %s MHz, band ends 2483.5" % (hi, 2400 + hi if hi is not None else "?"))
case("kChanMin is the band floor", lo == 0, "%s -> %s MHz" % (lo, 2400 + (lo or 0)))
case("the register would allow more", hi is not None and hi < 125,
     "RF_CH accepts 125, which is 2525 MHz")


def body(fn):
    m = re.search(r"\n(?:bool|void|uint8_t)\s+%s\s*\([^)]*\)\s*\{" % fn, cpp)
    if not m:
        return None
    i, depth = m.end() - 1, 0
    for j in range(i, len(cpp)):
        if cpp[j] == "{":
            depth += 1
        elif cpp[j] == "}":
            depth -= 1
            if depth == 0:
                return cpp[i:j]
    return None


# ── the sequence that matters ────────────────────────────────────────────
sample = body("sampleRpd")
case("sampleRpd exists", sample is not None)

if sample:
    order = [t for t in re.findall(r"ceLow\(\)|ceHigh\(\)|readReg\(REG_RPD\)", sample)]
    case("CE drops before the retune", order[:1] == ["ceLow()"],
         " -> ".join(order))
    case("RPD is read with CE up",
         "ceHigh()" in order and "readReg(REG_RPD)" in order
         and order.index("ceHigh()") < order.index("readReg(REG_RPD)"))
    # The one this file exists for.
    case("CE drops again after the read",
         "readReg(REG_RPD)" in order
         and "ceLow()" in order[order.index("readReg(REG_RPD)") + 1:],
         "without this the latch reports the whole band busy")
    case("the synthesiser settle is honoured", "kSettleUs" in sample)

rx = body("startReceiver")
case("startReceiver exists", rx is not None)
if rx:
    case("receiver does not set CONT_WAVE", "RF_CONT_WAVE" not in rx,
         "a carrier while listening would hear itself")
    case("receiver is primary RX", "CFG_PRIM_RX" in rx)
    case("any carrier is stopped first", re.search(r"\bstop\(\)", rx) is not None)

print()
if failed:
    print("FAILED: %s" % ", ".join(failed))
    sys.exit(1)
print("ok -- %d checks" % checks)
print("the RPD path reads one channel at a time, and lets go of the latch")
