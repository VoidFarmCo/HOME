#!/usr/bin/env python3
"""Settings survive a reboot, and one switch means one thing everywhere.

Two failures this catches, both of which look like nothing at all until a
user reports it weeks later.

A field added to AppSettings and wired to a row on the screen, but not to
settingsLoad and settingsSave. It works perfectly for the whole session you
test it in. It is gone at the next boot, and the report you get is "settings
don't stick", which sends you reading the SD code rather than the one line
that was never written.

And a switch row that the screen enumerates in more places than it declares.
Before the table, adding a setting meant four edits -- drawAll,
redrawIfChanged, handleTouch and the key handler -- and making three of them
gave you a row you could see and select but not change, or one that changed
and did not repaint. So the table is checked for being the only enumeration:
no sel== arm may name a switch row's index.

Last, the "Log to SD" switch is a promise about what the firmware writes to
somebody's card. It is worth exactly as much as the number of log-open sites
that ask before writing, so those are listed here by name and checked.

    python tools/check_settings.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

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


def read(rel):
    return (SKETCH / rel).read_text(encoding="utf-8", errors="replace")


def struct_fields(src):
    """AppSettings -> [(type, name)] in declaration order."""
    body = src[src.index("struct AppSettings {"):]
    body = body[:body.index("\n};")]
    out = []
    for m in re.finditer(r"^\s{2}(\w+)\s+(\w+)\s*=", body, re.M):
        out.append((m.group(1), m.group(2)))
    return out


def func_body(src, signature):
    """Text of the function whose definition starts with `signature`, by brace
    matching. Returns "" when the signature is not found, which the caller
    reports rather than silently passing."""
    i = src.find(signature)
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


def main():
    store_h = read("SettingsStore.h")
    store_c = read("SettingsStore.cpp")
    utils = read("utils.cpp")

    print("every setting is written to the card and read back:")
    fields = struct_fields(store_h)
    ok("found the AppSettings fields", len(fields) >= 7, str(fields))
    load = func_body(store_c, "bool settingsLoad()")
    save = func_body(store_c, "bool settingsSave()")
    ok("found settingsLoad and settingsSave", bool(load) and bool(save))
    for _type, name in fields:
        # The four touch bounds are persisted under a nested "touch" object
        # with short keys, and are rewritten from the board profile when the
        # saved board does not match. They are checked by name below instead.
        if name.startswith("touch"):
            continue
        ok("  %s is loaded" % name, name in load,
           "in AppSettings and not in settingsLoad -- it resets every boot")
        ok("  %s is saved" % name, name in save,
           "in AppSettings and not in settingsSave -- it is never written")
    for name in [n for _t, n in fields if n.startswith("touch")]:
        ok("  %s is persisted" % name, name in load and name in save)

    # And somebody has to call the loader.
    #
    # This is the rule the whole section was missing. Every field was checked
    # into settingsLoad() and settingsLoad() was never reached on the board
    # this firmware runs on: setup() had it inside `#if BOARD_HAS_ESP32S3`,
    # the CYD takes the #else, and nothing else in the tree called it. Save
    # wrote a perfectly good file that nothing ever read, on every boot since
    # the fork, and every check above passed the whole time.
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    calls, cyd_calls = 0, 0
    # A line-by-line walk of the #if stack with BOARD_HAS_ESP32S3 = 0, which
    # is what a CYD compiles as. Counting #ifs and subtracting, which is what
    # this did first, gets the answer right for the file it was written
    # against and wrong the moment a branch moves -- it passed a mutant that
    # deleted the only call the CYD reaches.
    stack = []          # True where this board takes the branch
    for line in ino.splitlines():
        t = line.strip()
        if t.startswith("#if "):
            cond = t[4:].strip()
            if cond == "BOARD_HAS_ESP32S3":
                stack.append(False)
            elif cond == "!BOARD_HAS_ESP32S3":
                stack.append(True)
            else:
                stack.append(True)          # unrelated condition, assume taken
        elif t.startswith("#ifdef ") or t.startswith("#ifndef "):
            stack.append(True)
        elif t.startswith("#else"):
            if stack:
                stack[-1] = not stack[-1]
        elif t.startswith("#endif"):
            if stack:
                stack.pop()
        elif "settingsLoad()" in t and not t.startswith("*") and "//" not in t[:t.index("settingsLoad()")]:
            calls += 1
            if all(stack):
                cyd_calls += 1
    ok("something calls settingsLoad()", calls > 0)
    ok("  and the CYD reaches one of them", cyd_calls > 0,
       "settingsLoad() is only called under BOARD_HAS_ESP32S3 -- this board "
       "boots on defaults every time, however well it saves them")

    print("\nthe settings screen declares its rows once:")
    ui = utils[utils.index("namespace AppSettingsUI {"):]
    ui = ui[:ui.index("\nnamespace SdFileManager")]

    table = ui[ui.index("kMainSwitches[] = {"):]
    table = table[:table.index("\n};")]
    rows = re.findall(r'\{"([^"]+)",\s*&AppSettings::(\w+)\s*,\s*'
                      r'(?:&AppSettings::(\w+)|nullptr)\s*\}', table)
    # Against the braces in the table rather than a minimum count. A minimum
    # is a guess that goes stale the moment a row is removed -- it was 3, and
    # dropping the inert NeoPixel row failed this line rather than any real
    # rule. Counting entries catches the failure that matters: the regex
    # parsing some of the table and the rest of the checks running on a
    # partial list while reporting green.
    declared = len(re.findall(r'^\s*\{"', table, re.M))
    ok("found the switch table", rows and len(rows) == declared,
       "parsed %d of %d entries: %s" % (len(rows), declared, rows))
    field_names = {n for _t, n in fields}
    for label, a, b in rows:
        ok("  %s -> AppSettings::%s" % (label, a), a in field_names,
           "no such field")
        if b:
            ok("  %s also moves %s" % (label, b), b in field_names,
               "no such field")

    fixed = re.search(r"kFixedRows\[\]\s*=\s*\{([^}]*)\}", ui)
    ok("found the bespoke rows", fixed is not None)
    n_fixed = len(re.findall(r'"', fixed.group(1))) // 2 if fixed else 0
    ok("  three of them", n_fixed == 3, "%d" % n_fixed)

    # The mistake this prevents: a switch row that some code paths reach
    # through the table and others through a leftover `sel == 4`. Indices
    # below kFirstSwitch are the bespoke widgets and are meant to be named.
    stray = sorted({int(m) for m in re.findall(r"sel\s*==\s*(\d+)", ui)
                    if int(m) >= n_fixed})
    ok("no sel== arm names a switch row", not stray,
       "found sel==%s -- switch rows go through the table" % stray)

    # This used to check two static_asserts that capped each page at what the
    # 2.8" fits. The list scrolls now, so the cap is gone and the thing worth
    # checking is that the window is derived from the panel rather than
    # written down -- a hard-coded visible count is the same bug with an
    # extra step, and it is the 3.5" that pays for it.
    ok("the list scrolls",
       "kVisibleRows" in ui and "scrollTop" in ui and "scrollToShow" in ui,
       "without scrolling the row count is capped by the shorter panel")
    ok("  and sizes its window from the panel",
       re.search(r"kVisibleRows\s*=\s*\(kToastTop - kListTop", ui) is not None,
       "a hard-coded visible count shows the 3.5\" fewer rows than it fits")
    # A count of rowVisible() calls is not a rule -- drop one and there are
    # still plenty. The loop that turns a touch into a row is the one that
    # has to skip, because rowRect() maps through the scroll window: without
    # the skip, a tap lands on whatever row now occupies that slot and the
    # one it names is somewhere off the screen.
    hit = re.search(r"for \(int i=0;i<rowCount\(\);\+\+i\)\{(.{0,240}?)\n  \}",
                    ui, re.S)
    ok("  and touch only reaches rows that are on screen",
       hit is not None and "rowVisible" in hit.group(1),
       "a tap selects whatever row now sits in that slot")
    ok("  and incremental redraws skip scrolled-away rows",
       ui.count("rowVisible(") >= 5,
       "a row drawn by index lands in whatever slot holds that number")
    ok("there is a row that opens the logging page",
       "kLinkRow" in ui and "goToPage(Page::Logging)" in ui)
    ok("leaving the logging page lands on Settings",
       ui.count("goToPage(Page::Main)") >= 2,
       "Back and Select should agree; one of them exits the feature instead")
    ok("entering Settings always lands on Settings",
       re.search(r"page\s*=\s*Page::Main;", ui[ui.index("void setup()"):])
       is not None,
       "the page is a static and would remember the last visit")

    print("\nthe SD Logging page is generated from the LogApp table:")
    # `Surveillance = 0,` has a space before the `=`, and a greedy \w+
    # followed by a bare [,=] does not match it -- which silently drops the
    # first enumerator and makes the count off by one. Allow the space.
    # Bounded relative to the enum, not by the first "kCount" in the whole
    # header: the comment above the enum mentions kCount, so an absolute
    # index landed BEFORE the enum and sliced an empty string -- which read
    # as "no enumerators" rather than as a broken check.
    blk = store_h[store_h.index("enum class LogApp"):]
    blk = blk[:blk.index("};")]
    enums = [e for e in re.findall(r"^\s*(\w+)\s*[,=]", blk, re.M)
             if e not in ("LogApp", "kCount")]
    ok("found the LogApp list", len(enums) >= 5, str(enums))
    entries = re.findall(r'\{"([^"]+)",\s*&AppSettings::(\w+)\}', store_c)
    ok("kLogApps has an entry per LogApp", len(entries) == len(enums),
       "%d entries for %d features: %s" % (len(entries), len(enums), entries))
    for _label, fld in entries:
        ok("  kLogApps -> AppSettings::%s" % fld, fld in field_names,
           "no such field")
    ok("the page sizes itself from the table",
       "kLogRows = 1 + (int)LogApp::kCount" in ui,
       "a hand-counted row list drops the feature you just added")
    ok("and reads its rows from it",
       "kLogApps[i - 1].label" in ui and "kLogApps[i - 1].field" in ui)

    print("\nevery log-open site asks before it writes:")
    sites = [
        ("Spotter.cpp",   "bool captureStart()",      "Surveillance capture",
         "Surveillance"),
        ("subghz.cpp",    "static void logEvent(",    "Jamming Detector log",
         "JamDetector"),
        ("wifi.cpp",      "static void pcapStart()",  "Packet Monitor pcap",
         "PacketMonitor"),
        ("bluetooth.cpp", "static bool esbOpenLogFile()", "ESB Sniffer log",
         "EsbSniffer"),
    ]
    for fname, sig, what, app in sites:
        body = func_body(read(fname), sig)
        ok("%s (%s)" % (what, fname), bool(body) and "sdLoggingAllowed" in body,
           "no body found for %r" % sig if not body
           else "opens a log without calling sdLoggingAllowed()")
        # Naming the wrong feature is the quiet version of not asking at all:
        # the site obeys a switch, just not its own, and the screen and the
        # behaviour disagree in a way nothing else would show.
        ok("  and names LogApp::%s" % app,
           bool(body) and ("LogApp::%s" % app) in body,
           "asks on behalf of some other feature")

    # Wardriving has two of them -- a background task and a foreground
    # session -- and they are not separable by function name, so they are
    # found by the path they build.
    gps = read("gps.cpp")
    wd = [m.start() for m in re.finditer(r'"/wd_%lu\.csv"', gps)]
    ok("found both wardrive log sites", len(wd) == 2, "%d" % len(wd))
    for i, at in enumerate(wd):
        # Bounded by the site's own `if (!logf)` rather than by a character
        # count: a fixed window either clips the gate when a comment grows or
        # reaches far enough to find the *other* site's gate and call it this
        # one's. The first attempt here was 400 characters and did both.
        end = gps.find("if (!logf)", at)
        ok("  wardrive site %d is bounded" % (i + 1), end > at)
        window = gps[at:end] if end > at else ""
        ok("  wardrive site %d asks" % (i + 1),
           "sdLoggingAllowed(LogApp::Wardriver)" in window,
           "opens /wd_*.csv without asking on its own behalf")

    ok("the accessor takes a feature", "bool sdLoggingAllowed(LogApp app)" in store_h)
    acc = func_body(store_c, "bool sdLoggingAllowed(LogApp app)")
    ok("  and requires the master switch", "logToSd" in acc,
       "per-feature switches with no master is not what the screen says")
    ok("  and the feature\'s own switch", "kLogApps[i].field" in acc)
    ok("  and bounds the index", "LogApp::kCount" in acc and "i < 0" in acc,
       "a bad index reads a bool out of some other setting and calls it "
       "permission")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
