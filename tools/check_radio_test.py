#!/usr/bin/env python3
"""Radio Test probes without the CC1101 driver's unbounded MISO wait.

ELECHOUSE_CC1101's Init(), getCC1101() and SpiReadStatus() all open with
`while (digitalRead(MISO_PIN));` and no deadline. With no module wired MISO
floats high and the first one never returns, so on a bare board Radio Test froze
before its loop ran and you could not exit. (It shipped that way once; this is
the check that was added when it was fixed.)

So Radio Test's CC1101 probe must use subghzCc1101Present() (a bounded
PARTNUM/VERSION read) and must not reach the ELECHOUSE library at all. The other
probes already return on a bare board: Nrf24Raw reads registers with a timeout,
and the PN532 firmware read times out.

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

RT = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "RadioTest.cpp"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    print(("  ok    " if cond else "  FAIL  ") + name + ("" if cond else (("  -- " + detail) if detail else "")))
    if not cond:
        FAILED.append(name)


def main():
    src = RT.read_text(encoding="utf-8", errors="replace")

    ok("CC1101 probe uses the bounded subghzCc1101Present()",
       "subghzCc1101Present()" in src,
       "without it the probe can hang on a bare board")
    ok("Radio Test does not call ELECHOUSE Init/getCC1101/SpiReadStatus",
       re.search(r"ELECHOUSE_cc1101\s*\.\s*(Init|getCC1101|SpiReadStatus)\s*\(", src) is None,
       "those busy-wait on MISO with no deadline")
    ok("Radio Test does not include the ELECHOUSE header",
       "ELECHOUSE_CC1101_SRC_DRV.h" not in src,
       "keep the hanging driver out of the probe")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
