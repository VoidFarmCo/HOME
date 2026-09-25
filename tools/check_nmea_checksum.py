#!/usr/bin/env python3
"""Checks that NMEA sentences are validated before anything parses them.

The checksum is the one field that says whether the rest of the sentence
survived the wire, and until 0.4.11 this tree threw it away: stripChecksum()
truncated at the '*' and nothing looked at the two hex digits.

That fails quietly, which is why it is worth a checker. A corrupted RMC is
still a well formed RMC. It has a latitude, a longitude and a timestamp, all
of them plausible, and the wardriver writes them to the card as a real fix.
A dropped comma is worse: splitCommaFields() shifts every field left, so a
longitude is parsed as a latitude and the result is a point in the wrong
hemisphere with nothing anywhere saying so.

Two things are guarded.

The arithmetic, transcribed here from the NMEA 0183 definition rather than
from gps.cpp, and run against real sentences and against mutations of them.
If the transcription and the firmware ever disagree about what a checksum
is, one of them is wrong and this says so.

The placement. The check has to sit in dispatchNmeaLine() ahead of every
handler call, because that is the single point all four parsers are reached
from. A fifth handler added below it is covered; one added above it is not.

Reads source and runs arithmetic; needs no board.

    python tools/check_nmea_checksum.py
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "gps.cpp"

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


def model(line):
    """NMEA 0183: XOR of every byte between '$' and '*', two hex digits.

    A missing field is a failure, not a pass. Truncation is the corruption
    most likely to occur, and a sentence cut before its '*' would otherwise
    sail through with its remaining fields intact and wrong.
    """
    star = line.find("*")
    if star < 0:
        return False
    tail = line[star + 1:star + 3]
    if len(tail) < 2 or not all(c in "0123456789abcdefABCDEF" for c in tail):
        return False
    total = 0
    for ch in line[1:star]:
        total ^= ord(ch)
    return total == int(tail, 16)


GOOD = [
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47",
    "$GPRMC,123519,A,4807.038,N,01131.000,E,022.4,084.4,230394,003.1,W*6A",
    "$GPGSA,A,3,04,05,,09,12,,,24,,,,,2.5,1.3,2.1*39",
    "$GPGSV,2,1,08,01,40,083,46,02,17,308,41,12,07,344,39,14,22,228,45*75",
]


def main():
    src = SRC.read_text(encoding="utf-8", errors="replace")

    print("the arithmetic, against real sentences:")
    for s in GOOD:
        ok("  accepts %s" % s[1:6], model(s), s)

    print("\nand against corruption:")
    MUT = [
        (GOOD[0].replace("4807.038", "4807.039"), "one digit changed"),
        (GOOD[1][:40], "truncated before the star"),
        (GOOD[2].replace("*39", "*3"), "half a checksum"),
        (GOOD[3].replace(",", "", 1), "a comma dropped"),
        (GOOD[0].replace("*47", "*48"), "checksum itself changed"),
    ]
    # Not tested: a byte appended after the checksum field. NMEA does not
    # cover anything past the two hex digits, which is why stripChecksum()
    # also has to drop a trailing '\r'. Asserting that it is rejected would
    # be asserting something the standard does not say, and this checker had
    # that case until it failed and the expectation turned out to be the
    # thing that was wrong.
    for line, why in MUT:
        ok("  rejects %s" % why, not model(line))

    print("\nthe firmware agrees about what a checksum is:")
    fn = re.search(r"static bool nmeaChecksumOk\(const char\* line\) \{(.*?)\n\}",
                   src, re.S)
    ok("nmeaChecksumOk() exists", fn is not None)
    body = fn.group(1) if fn else ""
    ok("  finds the field with strchr('*')", "strchr(line, '*')" in body)
    ok("  rejects a sentence with no '*'", "if (!star)" in body)
    ok("  requires two hex digits", body.count("isxdigit") == 2)
    ok("  XORs from line + 1", "p = line + 1" in body.replace(" ", " "))
    ok("  XORs up to the '*'", "p < star" in body)
    ok("  reads the field as base 16", "16)" in body and "strtoul" in body)

    print("\nand checks before it parses:")
    disp = re.search(r"void dispatchNmeaLine\(char\* line\) \{(.*?)\n\}", src, re.S)
    ok("dispatchNmeaLine() exists", disp is not None)
    d = disp.group(1) if disp else ""
    call = d.find("nmeaChecksumOk(line)")
    first = min([i for i in
                 [d.find("handleGsvSentence"), d.find("handleGsaSentence"),
                  d.find("handleGgaSentence"), d.find("handleRmcSentence")]
                 if i >= 0] or [-1])
    ok("  the check runs before any handler", call >= 0 and first > call,
       "check at %d, first handler at %d" % (call, first))
    ok("  and every handler is reached from here",
       all(h in d for h in ("handleGsvSentence", "handleGsaSentence",
                            "handleGgaSentence", "handleRmcSentence")))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
