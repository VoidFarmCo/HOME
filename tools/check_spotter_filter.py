#!/usr/bin/env python3
"""The Surveillance filter hides rows and nothing else.

A filter on a detector is a feature with one dangerous failure mode, and it
is not crashing. It is that a filtered list and a quiet street look
identical. Somebody sets Strong-only in a car park, walks somewhere that
matters, reads an empty screen, and concludes there is nothing there.

Three things keep that from happening and all three are easy to undo by
accident, so they are asserted here rather than left to the comments that
explain them.

  display only     record(), captureNote() and the SD writers never consult
                   the filter. Everything is detected, counted and logged
                   whatever the list is showing. A filter that edited the
                   capture would make the file on the card depend on what
                   somebody had selected while it ran, which is unreadable
                   six months later.

  per session      setup() resets it. A remembered filter is one somebody
                   forgets is on, and what that produces is a clean street
                   that was never actually looked at.

  it says so       the header prints shown/total whenever the filter is not
                   showing everything, so the screen itself carries the
                   difference between "four things here" and "four of
                   thirteen things here".

    python tools/check_spotter_filter.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "Spotter.cpp"

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


def strip_comments(s):
    """Comments are not code.

    Every assertion below is a substring search, and a commented-out line
    still contains its own substring: /* s_kindMask = kAllKinds; */ satisfies
    a search for "s_kindMask = kAllKinds" exactly as well as the real thing.
    That is not hypothetical here -- it is how the first version of this
    check passed while the per-session reset was commented out, which is the
    one failure that makes a filter dangerous rather than annoying.

    check_menu_dispatch.py learned the same lesson about a commented-out
    call. Stripping first is the fix in both.
    """
    s = re.sub(r"/\*.*?\*/", " ", s, flags=re.S)
    return re.sub(r"//[^\n]*", " ", s)


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
                return strip_comments(src[j:k + 1])
    return ""


def main():
    src = SRC.read_text(encoding="utf-8", errors="replace")

    print("the filter is consulted by the list and by nothing else:")
    ok("shown() exists", "bool shown(const Hit& h)" in src)

    # The whole assertion. If any of these learn about the filter, the
    # feature stops being a view and starts being an edit.
    for fn, sig, what in (
            ("record", "void record(", "the hit table"),
            ("captureNote", "void captureNote(", "the capture ring"),
            ("captureFlush", "void captureFlush(", "the SD writer")):
        b = body(src, sig)
        ok("%-12s does not consult it (%s)" % (fn + "()", what),
           bool(b) and "shown(" not in b and "s_kindMask" not in b
           and "s_minConf" not in b,
           "a filter that reaches here edits the evidence")

    # And the readers that should.
    dl = body(src, "void drawList(")
    ok("drawList() does consult it",
       "shownCount()" in dl and "nthShown(" in dl,
       "the list is the only thing the filter is for")

    print("\nit resets when the screen opens:")
    st = body(src, "void spotterSetup(") or body(src, "void setup(")
    ok("setup() found", bool(st))
    ok("  it resets the kind mask", "s_kindMask = kAllKinds" in st,
       "a filter that survives the screen is one somebody forgets is on")
    ok("  it resets the confidence floor", "s_minConf  = 0" in st
       or "s_minConf = 0" in st)
    ok("  and closes the filter screen", "s_filtOpen = false" in st,
       "reopening into the filter screen is a feature that looks broken")

    print("\nthe header says when it is not showing everything:")
    hdr = body(src, "void drawHeader(")
    ok("drawHeader() found", bool(hdr))
    ok("  it asks whether the filter is on", "filterIsAll()" in hdr,
       "a filtered list is indistinguishable from a quiet street")
    ok("  and prints shown/total when it is not",
       re.search(r'"%d/%d"', hdr) is not None and "shownCount()" in hdr,
       "the second number is the one that has not changed")

    print("\nthe mask covers every kind the signatures produce:")
    # A Kind missing from kFiltKinds cannot be switched off, which is a
    # control that silently does not exist. A Kind in the list that no
    # signature produces is a row that never does anything.
    sigs = (SRC.parent / "SpotterSignatures.h").read_text(encoding="utf-8",
                                                          errors="replace")
    m = re.search(r"enum class Kind : uint8_t \{([^}]*)\}", sigs, re.S)
    ok("the Kind enum was found", m is not None)
    if m:
        kinds = [k.strip().split("=")[0].strip()
                 for k in m.group(1).replace("\n", " ").split(",") if k.strip()]
        kinds = [k for k in kinds if k and k != "Unknown"]
        fl = re.search(r"const Kind kFiltKinds\[\] = \{(.*?)\};", src, re.S)
        ok("kFiltKinds was found", fl is not None)
        if fl:
            listed = re.findall(r"Kind::(\w+)", fl.group(1))
            missing = [k for k in kinds if k not in listed]
            extra = [k for k in listed if k not in kinds]
            ok("  every Kind can be switched off", not missing,
               "no filter row for %s, so it cannot be hidden" % missing)
            ok("  and no row is for a Kind that does not exist", not extra,
               "%s is in the filter and not in the enum" % extra)

    print("\nthe mask is wide enough for the enum:")
    m = re.search(r"kAllKinds = 0x([0-9A-Fa-f]+)", src)
    ok("kAllKinds was found", m is not None)
    if m and 'kinds' in dir():
        need = len(kinds) + 1          # + Unknown at bit 0
        got = bin(int(m.group(1), 16)).count("1")
        ok("  it has a bit per Kind", got >= need,
           "mask has %d bits for %d kinds: the ones past the end can never "
           "be set, so those rows would be permanently hidden" % (got, need))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
