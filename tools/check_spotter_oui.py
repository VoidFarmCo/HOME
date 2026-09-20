"""Check which OUI signatures reach the BLE path, and that none of them
should not.

Spotter matches the whole OUI table against WiFi source addresses. Against
BLE addresses it matches only the entries marked Conf::Strong, and only when
the advertised address is public. `Conf::Strong` is therefore doing two jobs:
it grades the finding for the user, and it decides whether an entry is
allowed on BLE at all.

That is the part worth guarding. Most of the table is contract manufacturers
and module vendors -- 23 Liteon blocks, 2 Espressif, Qualcomm Atheros --
whose hardware is in an enormous amount of unrelated consumer kit. On WiFi
they are graded Weak and read as corroboration. On BLE, where a scan hears
every advertiser in the room rather than just the ones probing for a network,
an Espressif block would report a shelf of dev boards as surveillance
hardware, and the device running the scan is itself an ESP32.

Nothing in C++ stops someone promoting one of those to Strong while thinking
only about how a WiFi hit is labelled. This is the check that notices.

    python tools/check_spotter_oui.py
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SIGS = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "SpotterSignatures.h")
SRC = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "Spotter.cpp")

# {{0xB4, 0x1E, 0x52}, Kind::Alpr, Conf::Strong,  "Flock Safety"},
ENTRY = re.compile(
    r"\{\s*\{\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,"
    r"\s*0x([0-9A-Fa-f]{2})\s*\}\s*,\s*Kind::(\w+)\s*,\s*Conf::(\w+)\s*,"
    r'\s*"([^"]*)"\s*\}')

CONF_ENUM = re.compile(
    r"enum\s+class\s+Conf\s*:\s*uint8_t\s*\{([^}]*)\}")

# The blocks the vendors hold themselves. An OUI is a claim about who built
# the radio; on BLE that claim is only worth acting on when it comes from a
# block IEEE assigned to the vendor whose product is being reported, not to
# whoever assembled it.
VENDOR_OWN = {
    "B4:1E:52": "Flock Safety",
    "00:25:DF": "Axon Enterprise",
}

# Assignees that must never reach the BLE path however they are labelled.
# Substring match against the entry's own label, which carries the name the
# IEEE registry gives rather than the product it was found in.
NEVER_ON_BLE = ("Liteon", "Espressif", "Qualcomm", "Atheros", "USI",
                "Samsung", "unregistered", "not a vendor", "LAA",
                "locally administered", "locally-administered")

fail = []


def check(cond, msg):
    if not cond:
        fail.append(msg)


text = io.open(SIGS, encoding="utf-8", newline="").read()
code = io.open(SRC, encoding="utf-8", newline="").read()

# ── the enum has to mean what the BLE filter assumes ────────────────────────
#
# The filter is `conf != Conf::Strong`, so it does not depend on the numeric
# value. What it does depend on is Strong continuing to be the top grade: add
# a `Certain` above it and entries that used to be the strongest evidence
# quietly stop being matched on BLE.
m = CONF_ENUM.search(text)
check(m is not None, "could not find the Conf enum")
if m:
    names = [n.strip().split("=")[0].strip()
             for n in m.group(1).split(",") if n.strip()]
    check(names == ["Weak", "Likely", "Strong"],
          "Conf is %r; the BLE filter assumes Strong is the top grade, so a "
          "new level above it needs a decision about whether it goes on BLE"
          % (names,))

# ── the table ───────────────────────────────────────────────────────────────
#
# Scoped to kOuiSigs rather than the whole file, so the BLE and name tables
# below it cannot quietly contribute entries to this check.
body = re.search(r"kOuiSigs\[\]\s*=\s*\{(.*?)\n\};", text, re.S)
check(body is not None, "could not find the kOuiSigs table")
entries = ENTRY.findall(body.group(1)) if body else []
check(len(entries) >= 30,
      "found only %d OUI entries; the regex has probably stopped matching"
      % len(entries))

# Every brace-opening line in the table has to have been understood. An entry
# the regex skips is an entry this check silently does not cover.
if body:
    braces = len(re.findall(r"^\s*\{\{", body.group(1), re.M))
    check(braces == len(entries),
          "%d entries in kOuiSigs but the regex matched %d; something in the "
          "table is written in a shape this check does not read"
          % (braces, len(entries)))

strong = []
for a, b, c, kind, conf, label in entries:
    oui = ("%s:%s:%s" % (a, b, c)).upper()
    first = int(a, 16)

    if conf == "Strong":
        strong.append((oui, label))

        check(oui in VENDOR_OWN,
              "%s (%s) is Strong, so it is matched against public BLE "
              "addresses. Only a vendor's own IEEE block should be: add it "
              "to VENDOR_OWN here if that is what it is, or grade it Likely."
              % (oui, label))

        check(not (first & 0x02),
              "%s (%s) is Strong but has the locally-administered bit set, "
              "so it is a randomised address rather than a vendor block and "
              "cannot mean what a Strong OUI is supposed to mean"
              % (oui, label))

        check(not (first & 0x01),
              "%s (%s) is Strong but is a group address, which no device "
              "uses as a source" % (oui, label))

        for bad in NEVER_ON_BLE:
            check(bad.lower() not in label.lower(),
                  "%s is labelled %r and graded Strong, which would put a "
                  "contract manufacturer's block on the BLE path"
                  % (oui, label))

    else:
        check(oui not in VENDOR_OWN,
              "%s (%s) is a vendor's own block but is graded %s, so it is "
              "no longer matched on BLE" % (oui, label, conf))

check(len(strong) == len(VENDOR_OWN),
      "%d Strong OUIs, expected %d. Every one of them is matched against "
      "public BLE addresses, so this number is a decision rather than an "
      "accident: %r" % (len(strong), len(VENDOR_OWN), strong))

for oui, want in VENDOR_OWN.items():
    got = [l for o, l in strong if o == oui]
    check(got, "%s (%s) is gone from the Strong set, so it no longer reaches "
               "the BLE path" % (oui, want))

# ── the filter is still in Spotter.cpp ──────────────────────────────────────
#
# Cheap, but the whole point of the table check is that something applies it.
check("BLE_ADDR_PUBLIC" in code,
      "Spotter.cpp no longer gates the BLE OUI match on a public address; an "
      "OUI read off a random address is noise")
check(re.search(r"conf\s*!=\s*Conf::Strong", code) is not None,
      "Spotter.cpp no longer filters the BLE OUI match to Strong entries")

checks = len(entries) * 4 + len(VENDOR_OWN) + 5
if fail:
    for f in fail:
        sys.stderr.write("FAIL: %s\n" % f)
    sys.exit(1)

print("%d OUI entries, %d reach the BLE path (%s)"
      % (len(entries), len(strong),
         ", ".join("%s %s" % (o, l) for o, l in strong)))
print("%d checks passed" % checks)
