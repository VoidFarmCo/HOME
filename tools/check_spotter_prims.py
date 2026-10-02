#!/usr/bin/env python3
"""The three matching primitives read the bytes they claim to.

Surveillance grew three tables it did not have: manufacturer data past the
company ID, service data past the UUID, and names matched anywhere rather
than at the start. Each one exists because a tracker hides its type in a
byte the old tables could not see.

Two of the three have an off-by-two waiting in them, and the failure is the
dangerous kind rather than the obvious kind.

NimBLE hands over manufacturer data with the company ID still on the front,
so an Apple Find My rule has to compare its prefix against md[2], not md[0].
Reading from md[0] compares the type byte 0x12 against the low half of the
company ID 0x004C. That does not crash and it does not report nothing: it
reports a confident, wrongly-labelled hit on whichever vendor happens to
have 0x12 in the right place, on a screen whose entire purpose is telling
somebody there is tracking hardware near them.

Service data has no such offset. NimBLE strips the UUID, so the prefix is
compared from byte zero, and borrowing the +2 from the manufacturer path
would silently skip the two bytes that identify the frame.

    python tools/check_spotter_prims.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "ESP32-DIV" / "SpotterSignatures.h"
SRC = ROOT / "ESP32-DIV" / "Spotter.cpp"

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


def body(src, sig):
    i = src.find(sig)
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


def table(src, name):
    """Rows of a signature table, as raw text."""
    m = re.search(r"static const \w+ " + name + r"\[\] = \{(.*?)\n\};",
                  src, re.S)
    if not m:
        return []
    txt = re.sub(r"/\*.*?\*/", "", m.group(1), flags=re.S)
    return [r for r in re.findall(r"\{[^{}]*(?:\{[^{}]*\}[^{}]*)*\}", txt)]


def main():
    sigs = SIGS.read_text(encoding="utf-8", errors="replace")
    src = SRC.read_text(encoding="utf-8", errors="replace")

    print("manufacturer data is read past the company ID:")
    mf = body(src, "bool mfgMatch(")
    ok("mfgMatch() found", bool(mf))
    # The whole check, in one assertion. md + 2, never md + 0.
    ok("  it compares from md + 2", "memcmp(md + 2" in mf,
       "reading from md[0] compares the payload against the company ID")
    ok("  and bounds the read against the length",
       re.search(r"len\s*<\s*\(size_t\)2\s*\+\s*sig\.prefixLen", mf)
       is not None,
       "a short advertisement would be read past its end")
    ok("  and refuses a prefixLen of 0",
       "sig.prefixLen == 0" in mf,
       "a zero-length prefix matches every device of that company")
    ok("  and refuses a prefixLen past the array",
       "sig.prefixLen > sizeof(sig.prefix)" in mf)

    print("\nservice data is read from byte zero:")
    sv = body(src, "bool svcDataMatch(")
    ok("svcDataMatch() found", bool(sv))
    ok("  it compares from sd, not sd + 2", "memcmp(sd, sig.prefix" in sv,
       "NimBLE strips the UUID, so a +2 here skips the frame type")
    ok("  and bounds the read", "len < sig.prefixLen" in sv)
    # prefixLen 0 is meaningful here, unlike in mfgMatch: DULT is the service
    # alone. So the check is the opposite one, that it is allowed.
    ok("  and treats prefixLen 0 as 'the service alone is the signal'",
       re.search(r"if \(sig\.prefixLen == 0\) \{\s*return true;", sv)
       is not None,
       "DULT has no prefix; demanding one would drop it")

    print("\nsubstring matching is anchored by nothing, so it is guarded:")
    nc = body(src, "bool nameContains(")
    ok("nameContains() found", bool(nc))
    ok("  it is case-insensitive", "strncasecmp" in nc)
    ok("  it honours minNameLen", "sig.minNameLen" in nc)
    ok("  and cannot run past the name",
       re.search(r"i \+ m <= n", nc) is not None,
       "the window has to fit inside the string")

    print("\nthe substring table runs after the anchored one:")
    for what, prefix_call, sub_call in (
            ("SSID", "nameMatch(ssid, kSsidSigs[i])",
             "nameContains(ssid, kNameInSigs[i])"),
            ("BLE name", "nameMatch(name.c_str(), kBleNameSigs[i])",
             "nameContains(name.c_str(), kNameInSigs[i])")):
        a, b = src.find(prefix_call), src.find(sub_call)
        ok("%-9s anchored first" % what, 0 <= a < b,
           "an anchored match is the better answer and must get first refusal")

    print("\nevery needle is long enough to mean something:")
    # A short needle has no anchor and no length guard worth having. Four is
    # the floor: "Tile" is four and is a real vendor name, three characters
    # is a coincidence waiting to happen.
    rows = table(sigs, "kNameInSigs")
    ok("kNameInSigs has rows", len(rows) > 0)
    short = []
    for r in rows:
        m = re.match(r'\{\s*"((?:[^"\\]|\\.)*)"\s*,\s*(\d+)', r)
        if not m:
            continue
        needle, minlen = m.group(1), int(m.group(2))
        if len(needle) < 4 and minlen == 0:
            short.append(needle)
    ok("  none shorter than 4 characters without a minNameLen", not short,
       "%s would match on coincidence" % short)

    print("\nthe tables are wired in at all:")
    for t, n in (("kMfgSigs", "kMfgSigCount"),
                 ("kSvcDataSigs", "kSvcDataSigCount"),
                 ("kNameInSigs", "kNameInSigCount")):
        ok("%-14s is iterated in Spotter.cpp" % t,
           re.search(r"i < " + n + r";", src) is not None
           and t + "[i]" in src,
           "a table nothing reads is a table that tests nothing")
        ok("  %-12s has a count" % t,
           re.search(r"constexpr size_t " + n + r"\s*=", sigs) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
