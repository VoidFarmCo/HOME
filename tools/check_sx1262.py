#!/usr/bin/env python3
"""The SX1262 driver sends the right command opcodes.

A LoRa radio driven with a wrong command byte is the classic silent bug: it
compiles, the BUSY handshake works, and the chip does nothing useful -- a packet
never goes out, or the radio sits in the wrong mode. There is no module wired yet
to catch it on hardware, so this pins the opcodes to the SX1262 datasheet (rev
2.1, table 11-1) and holds the network sync word PRIVATE (not the public/LoRaWAN
value). Reads source; needs no board.

    python tools/check_sx1262.py
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


# Datasheet opcodes.
OPS = {
    "OP_SET_STANDBY": 0x80, "OP_SET_PACKET_TYPE": 0x8A, "OP_SET_RF_FREQUENCY": 0x86,
    "OP_SET_MODULATION": 0x8B, "OP_SET_PACKET_PARAMS": 0x8C, "OP_SET_BUFFER_BASE": 0x8F,
    "OP_WRITE_BUFFER": 0x0E, "OP_READ_BUFFER": 0x1E, "OP_SET_TX": 0x83, "OP_SET_RX": 0x82,
    "OP_GET_IRQ_STATUS": 0x12, "OP_CLR_IRQ_STATUS": 0x02, "OP_SET_DIO_IRQ": 0x08,
    "OP_GET_RX_BUF_STATUS": 0x13, "OP_SET_TX_PARAMS": 0x8E, "OP_SET_PA_CONFIG": 0x95,
    "OP_SET_DIO2_RF_SW": 0x9D, "OP_SET_REGULATOR": 0x96, "OP_CALIBRATE": 0x89,
    "OP_WRITE_REGISTER": 0x0D, "OP_GET_STATUS": 0xC0,
}


def const_val(src, name):
    m = re.search(name + r"\s*=\s*(0x[0-9A-Fa-f]+)", src)
    return int(m.group(1), 0) if m else None


def main():
    h = SKETCH / "Sx1262.h"
    c = SKETCH / "Sx1262.cpp"
    ok("Sx1262.h exists", h.is_file())
    ok("Sx1262.cpp exists", c.is_file())
    if not c.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    src = c.read_text(encoding="utf-8", errors="replace")

    print("command opcodes match the datasheet:")
    for name, val in OPS.items():
        got = const_val(src, name)
        ok("%s == 0x%02X" % (name, val), got == val,
           "got %s" % (("0x%02X" % got) if got is not None else "MISSING"))

    print("\nprivate network + correct framing:")
    sw = const_val(src, "SYNCWORD")
    ok("sync word is PRIVATE (0x1424, not public 0x3444)", sw == 0x1424,
       "got %s" % (hex(sw) if sw is not None else "MISSING"))
    ok("present() reads GET_STATUS",
       re.search(r"present\b.*?OP_GET_STATUS", src, re.S) is not None)
    ok("send() waits on TX_DONE",
       re.search(r"send\b.*?IRQ_TX_DONE", src, re.S) is not None)
    ok("receive() checks RX_DONE and CRC",
       "IRQ_RX_DONE" in src and "IRQ_CRC_ERR" in src)
    ok("BUSY is polled before commands (waitBusy in cmd)",
       re.search(r"\bcmd\b[^{]*\{\s*waitBusy", src) is not None)
    ok("TXEN RF switch driven LOW for TX and HIGH for RX/idle",
       "LORA_TXEN" in src
       and re.search(r"writeAny\(LORA_TXEN, LOW\)", src) is not None
       and re.search(r"writeAny\(LORA_TXEN, HIGH\)", src) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
