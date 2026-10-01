#!/usr/bin/env python3
"""Where a logger writes both CSV and JSON, they describe the same row.

Three of the five log apps can choose their format. They write one record two
ways, and two spellings of one thing drift: a field added to the header and not
to the object, or renamed in one and not the other, gives you files that
disagree about what a row is. Nothing in the compiler notices, because both are
string literals.

The same mistake in a different shape shipped in 0.4.16, when a drawing that
was a copy of a table stopped matching it. check_solder_pads.py exists for that
one. This is the same assertion about the same kind of copy.

    python tools/check_log_fields.py

Reads source; needs no board.

The two that do not appear here
-------------------------------
The wardriver writes WiGLE's CSV because WiGLE's upload accepts nothing else,
and the packet monitor writes PCAP because that is what Wireshark reads.
Neither format is this project's to choose, so neither has a JSON spelling to
disagree with.

And the jam detector is here with nothing to compare. Its CSV never had a
header: six values a row and nothing naming them. The JSON names them, which
makes it the one case where the two formats genuinely differ in what they
carry, so all that can be checked is that the names exist.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SKETCH = os.path.join(os.path.dirname(HERE), "ESP32-DIV")

# the keys out of {\"ms\":%lu, ... as they appear in the C source
JSON_KEY = re.compile(r'\\"(\w+)\\":')

# (file, what it logs, regex for the CSV field list, or None when it has none)
LOGGERS = [
    ("Spotter.cpp", "Surveillance", re.compile(r'println\("((?:\w+,)+\w+)"\)')),
    ("bluetooth.cpp", "ESB sniffer",
     re.compile(r'println\("# format: ((?:\w+,)+\w+)"\)')),
    ("subghz.cpp", "jam detector", None),
]

# A CSV row written straight out of a printf, for the logger with no header.
# The names cannot be compared, but the count can: one key per column.
CSV_ROW = re.compile(r'printf\("((?:[^"\n]*,)+[^"\n]*)\\n"')


def json_fields(src):
    out = []
    for k in JSON_KEY.findall(src):
        if k not in out:
            out.append(k)
    return out


def main():
    problems = []
    checked = 0

    for name, what, csv_re in LOGGERS:
        path = os.path.join(SKETCH, name)
        src = open(path, encoding="utf-8").read()
        jf = json_fields(src)

        if not jf:
            problems.append("%s (%s): no JSON keys found; is the object still "
                            "built here?" % (name, what))
            continue

        if csv_re is None:
            m = CSV_ROW.search(src)
            if not m:
                problems.append("%s (%s): no CSV row printf found to count "
                                "columns against" % (name, what))
                continue
            cols = len(m.group(1).split(","))
            if cols != len(jf):
                problems.append("%s: %d CSV columns and %d JSON fields: %s"
                                % (what, cols, len(jf), jf))
                continue
            print("  ok    %-13s %d fields, matching the %d CSV columns it has "
                  "no header for: %s" % (what, len(jf), cols, ",".join(jf)))
            checked += 1
            continue

        m = csv_re.search(src)
        if not m:
            problems.append("%s (%s): no CSV field list found" % (name, what))
            continue
        cf = m.group(1).split(",")

        missing = [f for f in cf if f not in jf]
        extra = [f for f in jf if f not in cf]
        if missing:
            problems.append("%s: in the CSV header and not the JSON object: %s"
                            % (what, missing))
        if extra:
            problems.append("%s: in the JSON object and not the CSV header: %s"
                            % (what, extra))
        if not missing and not extra and cf != jf:
            problems.append("%s: same fields, different order: CSV %s, JSON %s"
                            % (what, cf, jf))
        if not missing and not extra and cf == jf:
            print("  ok    %-13s %d fields, same names and same order in both: %s"
                  % (what, len(cf), ",".join(cf)))
            checked += 1

    if problems:
        print()
        for p in problems:
            print("  FAIL  " + p)
        print()
        print("FAILED: %d" % len(problems))
        return 1

    print("\n%d loggers agree with themselves" % checked)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
