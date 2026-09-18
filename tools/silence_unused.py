#!/usr/bin/env python3
"""Clear the -Wunused-* noise so -Wall can be switched on for real.

Two different treatments, for two different reasons.

Unused *functions* are marked `__attribute__((unused))` rather than deleted.
They are already stripped -- the platform builds with -ffunction-sections
-fdata-sections and links with --gc-sections, and none of them appear in the
linked image -- so removing them saves no flash. What it would cost is a
conflict on every `git merge upstream/main`, which this fork is built to keep
cheap. A one-line prefix is the smaller footprint.

Unused *variables* are deleted. They are locals and file-scope statics, the
edits are small and self-contained, and a dead store marked "unused" reads
worse than no dead store.

Every edit is pinned to a line number AND an expected substring. Matching on
line text alone is not safe here: `int rssi;`, `int right = r.x + r.w - 6;`
and `static unsigned long lastSpamTime = 0;` each appear verbatim in other
functions where the variable IS read, and a text-driven pass deletes those
too. If any anchor does not match, nothing is written.

Line numbers refer to the tree as of the commit that added this script. Run
once; it is not idempotent (it will refuse rather than double-apply).
"""

import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
ATTR = "__attribute__((unused)) "

# (file, 1-based line, substring that must be on it)
MARK = [
    ("bluetooth.cpp", 140,  "static void bleWaitNavRelease"),
    ("bluetooth.cpp", 5414, "static int mjSharedCount()"),
    ("gps.cpp",       950,  "static const char* viewModeTag"),
    ("gps.cpp",       1960, "static const char* wardRadioModeLabel"),
    ("rfid.cpp",      574,  "static void rfidPrintWrappedStep"),
    ("rfid.cpp",      940,  "static int rfidInfoPanelPageCount"),
    ("rfid.cpp",      952,  "static void rfidDrawInfoPanel"),
    ("rfid.cpp",      1397, "static bool rfidRunTwoButtonDialog"),
    ("subghz.cpp",    92,   "static bool findLatestExportPath"),
    ("subghz.cpp",    783,  "static void replayToggleAuto()"),
    ("subghz.cpp",    1800, "static void profileRefreshSd()"),
    ("utils.cpp",     1867, "static void drawFooterButton"),
    # dead no-arg wrapper; every caller uses the bool overload
    ("wifi.cpp",      5629, "static void drawRevealScreen();"),
    ("wifi.cpp",      6236, "static void drawRevealScreen() {"),
    # dead void wrapper around paintTextLineEx
    ("wifi.cpp",      8144, "static void paintTextLine(int y"),
    ("wifi.cpp",      8347, "static void paintTextLine(int y"),
]

# (file, 1-based line, substring that must be on it)
DROP = [
    # PacketMonitor::draw() is dead in full: nothing calls it (the name
    # appears exactly once in the tree, at its own definition), and every
    # line of the body writes to a local that is then discarded --
    # getMultiplicator() is pure and there is no global `rssi` for those
    # stores to reach. Emptying the body changes nothing observable.
    ("wifi.cpp", 618, "double multiplicator = getMultiplicator();"),
    ("wifi.cpp", 619, "int len;"),
    ("wifi.cpp", 620, "int rssi;"),
    ("wifi.cpp", 622, "if (pkts[MAX_X - 1] > 0) rssi ="),
    ("wifi.cpp", 623, "else rssi = rssiSum;"),
    # DeauthDetect::icons -- file-scope array, never indexed
    ("wifi.cpp", 1559, "static const unsigned char* icons[ICON_NUM] = {"),
    ("wifi.cpp", 1560, "bitmap_icon_power,"),
    ("wifi.cpp", 1561, "bitmap_icon_go_back"),
    ("wifi.cpp", 1562, "};"),
    # softAPIP() is a getter; result discarded
    ("wifi.cpp", 3820, "int ip = WiFi.softAPIP();"),
    # four independent never-read spam timers, in four different functions
    ("wifi.cpp", 3934, "static unsigned long lastSpamTime = 0;"),
    ("wifi.cpp", 4696, "static unsigned long lastSpamTime = 0;"),
    ("wifi.cpp", 5384, "static unsigned long lastSpamTime = 0;"),
    ("wifi.cpp", 9725, "static unsigned long lastSpamTime = 0;"),

    ("utils.cpp", 644,  "uint16_t wifiColor ="),
    ("utils.cpp", 1777, "int right = r.x + r.w - 6;"),

    # initializeRadiosMultiMode, two copies in two namespaces. The flags are
    # written and never read; radioN.begin() and configureRadio() stay.
    ("bluetooth.cpp", 3240, "bool radio1Active = false;"),
    ("bluetooth.cpp", 3241, "bool radio2Active = false;"),
    ("bluetooth.cpp", 3242, "bool radio3Active = false;"),
    ("bluetooth.cpp", 3246, "radio1Active = true;"),
    ("bluetooth.cpp", 3250, "radio2Active = true;"),
    ("bluetooth.cpp", 3254, "radio3Active = true;"),
    ("bluetooth.cpp", 5133, "bool radio1Active = false;"),
    ("bluetooth.cpp", 5134, "bool radio2Active = false;"),
    ("bluetooth.cpp", 5135, "bool radio3Active = false;"),
    ("bluetooth.cpp", 5139, "radio1Active = true;"),
    ("bluetooth.cpp", 5143, "radio2Active = true;"),
    ("bluetooth.cpp", 5147, "radio3Active = true;"),
    # updateTFT(): four statics tracking previous state nothing compares against
    ("bluetooth.cpp", 3270, "static bool previousJammerState = false;"),
    ("bluetooth.cpp", 3271, "static bool prevNRF1State = false;"),
    ("bluetooth.cpp", 3272, "static bool prevNRF2State = false;"),
    ("bluetooth.cpp", 3273, "static int previousMode = -1;"),
    ("bluetooth.cpp", 8426, "int hidLen = 10;"),

    # declared, assigned once inside `if (!bgWas)`, never read. The
    # s_fgScanPaused assignment in that block is a real effect and stays.
    ("gps.cpp", 3674, "uint32_t linesWritten = 0;"),
    ("gps.cpp", 3675, "uint32_t scanCount = 0;"),
    ("gps.cpp", 3676, "uint32_t lastScanMs = 0;"),
    ("gps.cpp", 4055, "linesWritten = s_fgLines;"),
    ("gps.cpp", 4056, "scanCount = s_fgScans;"),
    ("gps.cpp", 4057, "lastScanMs = now;"),
]


# (file, 1-based line, substring that must be on it, replacement text)
REPLACE = [
    ("wifi.cpp", 617, "void draw() {",
     "// Dead: never called, and the body only wrote to discarded locals.\n"
     "void draw() {"),
]


def load(name):
    raw = (SKETCH / name).read_bytes()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    return raw.decode("utf-8", "surrogateescape").split(nl), nl


def main():
    names = ({f for f, _, _ in MARK} | {f for f, _, _ in DROP}
             | {f for f, _, _, _ in REPLACE})
    files = {n: load(n) for n in names}

    problems = []
    for label, table in (("MARK", MARK), ("DROP", DROP),
                         ("REPLACE", [(f, l, w) for f, l, w, _ in REPLACE])):
        for name, ln, want in table:
            lines, _ = files[name]
            if ln - 1 >= len(lines) or want not in lines[ln - 1]:
                actual = lines[ln - 1].strip() if ln - 1 < len(lines) else "<past EOF>"
                problems.append("%s %s:%d expected %r, found %r"
                                % (label, name, ln, want, actual[:70]))
    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("anchors stale; nothing written", file=sys.stderr)
        return 1

    for name, ln, _ in MARK:
        lines, _ = files[name]
        text = lines[ln - 1]
        indent = text[:len(text) - len(text.lstrip())]
        lines[ln - 1] = indent + ATTR + text.lstrip()

    for name, ln, _ in DROP:
        files[name][0][ln - 1] = None

    for name, ln, _, text in REPLACE:
        lines, nl = files[name]
        lines[ln - 1] = text.replace("\n", nl)

    for name, (lines, nl) in files.items():
        kept = [l for l in lines if l is not None]
        (SKETCH / name).write_bytes(nl.join(kept).encode("utf-8", "surrogateescape"))

    print("marked %d function definitions/declarations" % len(MARK))
    print("removed %d unused variable lines" % len(DROP))
    return 0


if __name__ == "__main__":
    sys.exit(main())
