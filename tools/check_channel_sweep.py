#!/usr/bin/env python3
"""Packet Monitor's channel-sweep (analyzer) mode stays safe and complete.

The Sweep slot turns Packet Monitor into a 2.4 GHz occupancy meter: the radio
hops 1..MAX_CH on a fixed dwell and each channel gets a bar for the frames
that landed on it. Three ways that can go wrong, and each is cheap to catch
here and expensive to catch on the board:

  1. An all-quiet band is the ordinary bench case, not an edge one. The bar
     heights are scaled to the busiest channel, so with nothing on the air
     the scale is zero and the height is a divide by zero. sweepDraw() guards
     it (maxVal == 0 -> 1); without the guard the feature faults the instant
     you open it somewhere quiet, which is most places.

  2. The hop has to visit every channel 1..MAX_CH and never land on 0 or
     MAX_CH+1 -- channel 0 is not a channel and esp_wifi_set_channel rejects
     it, and a sweep that skips 14 (or stalls on one) is a meter that lies.
     The wrap is (s_sweepCh % MAX_CH) + 1; this reimplements it and walks a
     full cycle to prove the set of visited channels is exactly {1..MAX_CH}.

  3. A bar must measure one dwell, not accumulate forever. The hop clears the
     incoming channel's count (s_chanPkts[s_sweepCh] = 0) so the next dwell
     starts fresh; drop that line and every bar climbs to the ceiling and
     stays, which looks like a busy band whether or not one is there.

  4. The mode is off on entry. Packet Monitor has always opened on the
     waterfall; the sweep only ever switches an already-open view. s_sweep
     starts false and ptmSetup() sets it false, so a re-entry after a sweep
     does not reopen in sweep.

Reads source. Needs no board.
"""
import re
import sys
from pathlib import Path

WIFI = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "wifi.cpp"

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


def squeeze(s):
    """Collapse runs of whitespace so a match does not care about layout."""
    return re.sub(r"\s+", " ", s)


def main():
    src = WIFI.read_text(encoding="utf-8", errors="replace")
    flat = squeeze(src)

    # MAX_CH, read from the source rather than assumed, so a band change moves
    # this check with it.
    m = re.search(r"#define\s+MAX_CH\s+(\d+)", src)
    max_ch = int(m.group(1)) if m else None
    ok("MAX_CH is defined", max_ch is not None)
    if max_ch is None:
        print()
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("MAX_CH = %d" % max_ch)
    print()

    # 1. divide-by-zero guard in the bar scaling.
    guard = re.search(r"if\s*\(\s*maxVal\s*==\s*0\s*\)\s*maxVal\s*=\s*1", flat)
    ok("sweepDraw guards maxVal == 0 before scaling", guard is not None,
       "an all-quiet band divides by zero")

    # 2. the hop wraps 1..MAX_CH. Find the expression and walk a full cycle.
    wrap = re.search(r"s_sweepCh\s*=\s*\(\s*uint8_t\s*\)\s*\(\s*\(\s*s_sweepCh\s*%\s*MAX_CH\s*\)\s*\+\s*1\s*\)", flat)
    ok("hop uses the (s_sweepCh %% MAX_CH) + 1 wrap", wrap is not None,
       "a different wrap can reach 0 or skip a channel")
    if wrap is not None:
        visited = set()
        c = 1                       # sweepReset() starts here
        for _ in range(max_ch * 3):  # several full cycles
            visited.add(c)
            ok_range = 1 <= c <= max_ch
            if not ok_range:
                break
            c = (c % max_ch) + 1
        ok("the wrap visits exactly channels 1..MAX_CH",
           visited == set(range(1, max_ch + 1)),
           "visited %s" % sorted(visited))

    # 3. the incoming channel's count is cleared on hop: the clear statement
    #    follows the wrap in source order (a comment between them is fine).
    clears = False
    hop = re.search(r"s_sweepCh\s*=\s*\(\s*uint8_t\s*\)\s*\(\s*\(\s*s_sweepCh\s*%\s*MAX_CH\s*\)\s*\+\s*1\s*\)\s*;", src)
    if hop is not None:
        after = src[hop.end():hop.end() + 200]
        clears = re.search(r"^\s*(//[^\n]*\n\s*)*s_chanPkts\s*\[\s*s_sweepCh\s*\]\s*=\s*0\s*;", after) is not None
    ok("hop clears the new channel's count", clears,
       "bars accumulate forever without it")

    # 4. the mode defaults off and setup resets it.
    default_off = re.search(r"bool\s+s_sweep\s*=\s*false", flat)
    setup_off = re.search(r"s_sweep\s*=\s*false\s*;\s*//[^\n]*entry", src)
    ok("s_sweep defaults to false", default_off is not None)
    ok("ptmSetup resets s_sweep to false", setup_off is not None,
       "a re-entry after a sweep would reopen in sweep")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
