#!/usr/bin/env python3
"""The CSV header and the JSON keys describe the same row.

The Spotter writes a capture in one of two formats, chosen by a setting. They
are two spellings of one record, and two spellings drift: a field added to the
header and not to the object, or renamed in one and not the other, gives you
files that disagree about what a row is. Nothing in the compiler notices,
because both are string literals.

The same mistake, in a different shape, shipped in 0.4.16: a drawing that was
a copy of a table and stopped matching it. check_solder_pads.py exists for
that one. This is the same assertion about the same kind of copy.

    python tools/check_log_fields.py

Reads source; needs no board.

What it does not check
----------------------
That the JSON is well formed when a network is named with a quote and a
newline. That is the interesting case and it wants bytes off the air, not a
regex over the source: the escaping is written out at Spotter.cpp and the
reason it exists is in the comment above it.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SPOTTER = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "Spotter.cpp")

CSV_HEADER = re.compile(r'println\("((?:\w+,)+\w+)"\)')
# the keys out of {\"ms\":%lu,\"mac\":\"...  as they appear in the C source
JSON_KEY = re.compile(r'\\"(\w+)\\":')


def main():
    src = open(SPOTTER, encoding="utf-8").read()

    m = CSV_HEADER.search(src)
    if not m:
        print("  FAIL  no CSV header literal found in Spotter.cpp")
        return 1
    csv_fields = m.group(1).split(",")

    # The object is built in one snprintf; take the keys in the order written.
    json_fields = []
    for k in JSON_KEY.findall(src):
        if k not in json_fields:
            json_fields.append(k)

    problems = []
    if not json_fields:
        problems.append("no JSON keys found; is the object still built here?")

    missing = [f for f in csv_fields if f not in json_fields]
    extra = [f for f in json_fields if f not in csv_fields]
    if missing:
        problems.append("in the CSV header and not the JSON object: %s" % missing)
    if extra:
        problems.append("in the JSON object and not the CSV header: %s" % extra)
    if not problems and csv_fields != json_fields:
        problems.append("same fields, different order: CSV %s, JSON %s"
                        % (csv_fields, json_fields))

    if problems:
        for p in problems:
            print("  FAIL  " + p)
        print()
        print("FAILED: %d" % len(problems))
        return 1

    print("  ok    %d fields, same names and same order in both formats: %s"
          % (len(csv_fields), ",".join(csv_fields)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
