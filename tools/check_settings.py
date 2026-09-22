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

    print("\nthe settings screen declares its rows once:")
    ui = utils[utils.index("namespace AppSettingsUI {"):]
    ui = ui[:ui.index("\nnamespace SdFileManager")]

    rows = re.findall(r'\{"([^"]+)",\s*&AppSettings::(\w+)\s*,\s*'
                      r'(?:&AppSettings::(\w+)|nullptr)\s*\}', ui)
    ok("found the switch table", len(rows) >= 3, str(rows))
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

    ok("the row list is bounded at compile time",
       "static_assert" in ui and "kToastTop" in ui,
       "nothing stops the next row drawing under the footer")

    print("\nevery log-open site asks before it writes:")
    sites = [
        ("Spotter.cpp",   "bool captureStart()",      "Surveillance capture"),
        ("subghz.cpp",    "static void logEvent(",    "Jamming Detector log"),
        ("wifi.cpp",      "static void pcapStart()",  "Packet Monitor pcap"),
        ("bluetooth.cpp", "static bool esbOpenLogFile()", "ESB Sniffer log"),
    ]
    for fname, sig, what in sites:
        body = func_body(read(fname), sig)
        ok("%s (%s)" % (what, fname), bool(body) and "sdLoggingAllowed" in body,
           "no body found for %r" % sig if not body
           else "opens a log without calling sdLoggingAllowed()")

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
        ok("  wardrive site %d asks" % (i + 1), "sdLoggingAllowed" in window,
           "opens /wd_*.csv without calling sdLoggingAllowed()")

    ok("the accessor exists and reads the setting",
       "bool sdLoggingAllowed()" in store_h
       and re.search(r"bool sdLoggingAllowed\(\)\s*\{\s*return\s+\w+\.logToSd;",
                     store_c) is not None)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
