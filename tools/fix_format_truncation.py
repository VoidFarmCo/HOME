#!/usr/bin/env python3
"""Clear the 14 -Wformat-truncation warnings in the sketch.

Twelve are buffers that a wide `%d` could overrun; those get widened to the
worst case the format can actually produce. `snprintf` was already truncating
safely, so this changes nothing except in cases that were being silently cut.

Two are not buffer-size problems and widening them would be the wrong fix.
Both parse NMEA fields straight off the GPS serial line, and neither checks
that what it got is a number, so the values fed to `%02d` are unbounded --
`formatUtcFromField` does digit arithmetic on arbitrary bytes, and
`wardDdMmYyToIso` takes whatever `sscanf("%d/%d/%d")` hands back. Validating
the input bounds the output to a fixed width, silences the warning, and stops
malformed sentences producing nonsense timestamps. That is the actual bug.

Line-anchored like tools/silence_unused.py, and for the same reason: several
of these declarations are identical text appearing in unrelated functions.

One-shot: this records an edit already applied to the tree, kept for
provenance. Re-running it will refuse, because the anchors no longer match.
formatUtcFromField also gained an h/m/s range check by hand afterwards --
gcc 8 does not carry the digit loop through to the reads at the snprintf, so
the loop alone did not bound %02d as far as the warning was concerned.
"""

import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

# (file, 1-based line, old declaration, new declaration, why)
WIDEN = [
    # "Page %d/%d" with two ints: 5 + 11 + 1 + 11 + NUL = 29
    ("wifi.cpp", 4390, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    ("wifi.cpp", 5079, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    ("wifi.cpp", 6026, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    ("wifi.cpp", 6881, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    ("wifi.cpp", 7464, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    ("wifi.cpp", 10331, "char page_buf[20];", "char page_buf[32];", "Page %d/%d"),
    # "%02d: %s" with name[16]: 11 + 2 + 15 + NUL = 29
    ("wifi.cpp", 7348, "char left[28];", "char left[48];", "%02d: %s over name[16]"),
    # "%02d: %d.%d.%d.%d": 11 + 2 + 15 + NUL = 29
    ("wifi.cpp", 7375, "char left[28];", "char left[48];", "%02d: dotted quad"),
    # "[!] cred %s / %s" with two char[32]: 9 + 31 + 3 + 31 + NUL = 75
    ("wifi.cpp", 8611, "char buf[48];", "char buf[80];", "cred %s / %s"),
    # "%d suspects nearby": 11 + 17 + NUL = 29
    ("bluetooth.cpp", 2491, "char found[24];", "char found[32];", "%d suspects nearby"),
    # "%d/%d": 11 + 1 + 11 + NUL = 24
    ("rfid.cpp", 350, "char pageLabel[12];", "char pageLabel[24];", "%d/%d"),
    ("ducky.cpp", 747, "char counter[20];", "char counter[24];", "%d/%d"),
]

# (file, old block, new block)
VALIDATE = [
    ("gps.cpp", """  int h = (field[0] - '0') * 10 + (field[1] - '0');
  int m = (field[2] - '0') * 10 + (field[3] - '0');
  int s = (field[4] - '0') * 10 + (field[5] - '0');
  snprintf(utcStr, sizeof(utcStr), "%02d:%02d:%02d", h, m, s);""",
     """  // This is a raw NMEA field off the serial line and the length check
  // above is the only thing it has passed. On non-digit bytes the arithmetic
  // below produces values well outside 0-99, and the formatted result runs
  // past utcStr. Reject anything that is not six digits.
  for (int i = 0; i < 6; i++) {
    if (field[i] < '0' || field[i] > '9') {
      strncpy(utcStr, "--:--:--", sizeof(utcStr));
      return;
    }
  }
  const int h = (field[0] - '0') * 10 + (field[1] - '0');
  const int m = (field[2] - '0') * 10 + (field[3] - '0');
  const int s = (field[4] - '0') * 10 + (field[5] - '0');
  snprintf(utcStr, sizeof(utcStr), "%02d:%02d:%02d", h, m, s);"""),

    ("gps.cpp", """  if (sscanf(ddmmyy, "%d/%d/%d", &d0, &m0, &y0) != 3) {
    return false;
  }
  int yFull = y0;""",
     """  if (sscanf(ddmmyy, "%d/%d/%d", &d0, &m0, &y0) != 3) {
    return false;
  }
  // sscanf returns whatever magnitude it parsed. Unbounded, these overrun
  // iso and write a nonsense date into the wigle export either way.
  if (d0 < 1 || d0 > 31 || m0 < 1 || m0 > 12 || y0 < 0 || y0 > 99) {
    return false;
  }
  int yFull = y0;"""),
]


def main():
    names = {f for f, *_ in WIDEN} | {f for f, *_ in VALIDATE}
    files = {}
    for n in names:
        raw = (SKETCH / n).read_bytes()
        nl = "\r\n" if b"\r\n" in raw else "\n"
        files[n] = (raw.decode("utf-8", "surrogateescape").split(nl), nl)

    problems = []
    for name, ln, old, _, _ in WIDEN:
        lines, _ = files[name]
        if ln - 1 >= len(lines) or old not in lines[ln - 1]:
            got = lines[ln - 1].strip() if ln - 1 < len(lines) else "<past EOF>"
            problems.append("WIDEN %s:%d expected %r, found %r" % (name, ln, old, got[:60]))
    for name, old, _ in VALIDATE:
        lines, nl = files[name]
        if nl.join(lines).count(old.replace("\n", nl)) != 1:
            problems.append("VALIDATE %s: block not found exactly once" % name)

    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("anchors stale; nothing written", file=sys.stderr)
        return 1

    for name, ln, old, new, _ in WIDEN:
        lines, _ = files[name]
        lines[ln - 1] = lines[ln - 1].replace(old, new)

    for name, old, new in VALIDATE:
        lines, nl = files[name]
        text = nl.join(lines).replace(old.replace("\n", nl), new.replace("\n", nl))
        files[name] = (text.split(nl), nl)

    for name, (lines, nl) in files.items():
        (SKETCH / name).write_bytes(nl.join(lines).encode("utf-8", "surrogateescape"))

    print("widened %d buffers" % len(WIDEN))
    print("added input validation at %d NMEA parse sites" % len(VALIDATE))
    return 0


if __name__ == "__main__":
    sys.exit(main())
