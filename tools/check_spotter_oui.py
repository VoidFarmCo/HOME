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
GPS = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "gps.cpp")

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
    "AC:9F:C3": "Ring",
    "18:7F:88": "Ring",
    "34:3E:A4": "Ring",
    "54:E0:19": "Ring",
    "5C:47:5E": "Ring",
    "64:9A:63": "Ring",
    "90:48:6C": "Ring",
    "9C:76:13": "Ring",
    "CC:3B:FB": "Ring",
    "C4:DB:AD": "Ring",
    "24:2B:D6": "Ring",
    "00:B4:63": "Ring",
    "50:E4:67": "Ring",
    "C0:56:E3": "Hikvision",
    "44:19:B6": "Hikvision",
    "28:57:BE": "Hikvision",
    "2C:AA:8E": "Wyze Labs",
    "D0:3F:27": "Wyze Labs",
    "7C:78:B2": "Wyze Labs",
    "00:40:8C": "Axis Comms",
    "B8:A4:4F": "Axis Comms",
    "E0:A7:00": "Verkada",
    "70:1A:D5": "Avigilon Alta",
    "0C:FA:22": "Flipper Devices",
}

# Assignees that must never reach the BLE path however they are labelled.
# Substring match against the entry's own label, which carries the name the
# IEEE registry gives rather than the product it was found in.
NEVER_ON_BLE = ("Liteon", "Espressif", "Qualcomm", "Atheros", "USI",
                "Murata", "Realtek", "Telink", "module",
                "Samsung", "unregistered", "not a vendor", "LAA",
                "locally administered", "locally-administered")

fail = []


def check(cond, msg):
    if not cond:
        fail.append(msg)


text = io.open(SIGS, encoding="utf-8", newline="").read()
code = io.open(SRC, encoding="utf-8", newline="").read()
gps = io.open(GPS, encoding="utf-8", newline="").read()

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

# ── name signatures: a short prefix has to declare its length ───────────────
#
# NameSig.exactLen exists because KARR advertises "QT " or "DR " followed by
# exactly eight characters. Matched case-insensitively and on the prefix
# alone, "dr " claims every device whose name starts that way -- and a match
# is what puts a row on the operator's screen.
#
# Length is a proxy for distinctiveness and not a very good one, so it only
# decides which rows have to be argued for. Below the threshold a prefix needs
# either an exact length or a line in SHORT_BUT_DELIBERATE saying why it is
# specific enough without one. At or above it ("Spectacles", "FS Ext Battery")
# a prefix carries its own specificity.
SHORT_PREFIX = 6

# Short prefixes that have been looked at and kept. The value is the reason,
# which is the point of the list: a bare word like "Flock" is distinctive in a
# way "dr " is not, and that is a judgement someone should have to write down.
SHORT_BUT_DELIBERATE = {
    "Flock": "a distinctive word, and already graded Likely for this reason",
}

# {"QT ", 11, Kind::Vehicle, Conf::Likely, "KARR BT module"},
NAME_ENTRY = re.compile(
    r'\{\s*"([^"]*)"\s*,\s*(\d+)\s*,\s*Kind::(\w+)\s*,\s*Conf::(\w+)\s*,'
    r'\s*"([^"]*)"\s*\}')

name_entries = []
for table in ("kSsidSigs", "kBleNameSigs"):
    body = re.search(r"%s\[\]\s*=\s*\{(.*?)\n\};" % table, text, re.S)
    check(body is not None, "could not find the %s table" % table)
    rows = NAME_ENTRY.findall(body.group(1)) if body else []
    check(rows, "%s parsed to nothing; the struct layout probably changed, so "
                "this check is silently testing an empty list" % table)
    for prefix, exact, kind, conf, label in rows:
        name_entries.append((table, prefix, int(exact), kind, conf, label))

for table, prefix, exact, kind, conf, label in name_entries:
    check(exact == 0 or exact >= len(prefix),
          '%s: "%s" declares length %d, shorter than the prefix itself, so it '
          "can never match" % (table, prefix, exact))

    check(len(prefix) >= SHORT_PREFIX
          or exact != 0
          or prefix in SHORT_BUT_DELIBERATE,
          '%s: "%s" is %d characters and declares no exact length, so it '
          "matches any name beginning that way. Give it a length, or add it "
          "to SHORT_BUT_DELIBERATE with the reason it is specific enough "
          "without one." % (table, prefix, len(prefix)))

    check(not (exact != 0 and conf == "Strong"),
          '%s: "%s" is graded Strong on a name and a length. A name is a '
          "string a device chose to broadcast, and its length does not "
          "corroborate it" % (table, prefix))

for prefix in SHORT_BUT_DELIBERATE:
    check(any(e[1] == prefix for e in name_entries),
          '"%s" is listed in SHORT_BUT_DELIBERATE but is no longer a name '
          "signature; the exemption outlived the row it was written for"
          % prefix)

# The two KARR rows specifically: they are why the field exists, and the
# published pattern is a prefix plus exactly eight characters.
karr = [e for e in name_entries if "KARR" in e[5]]
check(len(karr) == 2, "expected 2 KARR name signatures, found %d" % len(karr))
for table, prefix, exact, kind, conf, label in karr:
    check(exact == len(prefix) + 8,
          '%s: KARR row "%s" declares length %d; the published pattern is the '
          "prefix plus exactly eight, so it should be %d"
          % (table, prefix, exact, len(prefix) + 8))
    check(conf != "Strong",
          '%s: KARR row "%s" is graded Strong. A module being present is not '
          "evidence it is unpatched: the fix is applied by hand and the "
          "advertisement does not say either way" % (table, prefix))

# ── vehicle rows stay out of the WiGLE upload ─────────────────────
#
# The paper that documented KARR describes the attack beginning with a
# name-pattern query against a public wardriving database. Pueo not adding to
# that index is a decision, and a decision in one .cpp that another .cpp has
# to keep honouring is the kind that quietly stops being true.
check("isVehicleName" in code,
      "Spotter.cpp no longer defines isVehicleName")
check(re.search(r"kind\s*==\s*Kind::Vehicle", code) is not None,
      "Spotter::isVehicleName no longer selects on Kind::Vehicle, so it does "
      "not track what the signature table says is a vehicle")
# A call, not a mention: the comment beside it names the function too, and
# the first version of this check was satisfied by that comment while the
# call itself had been replaced with `if (false)`.
check(re.search(r"Spotter::isVehicleName\s*\(", gps) is not None,
      "gps.cpp no longer calls Spotter::isVehicleName; the WiGLE upload is "
      "back to exporting vehicle modules with their coordinates")
check(any(e[3] == "Vehicle" for e in name_entries),
      "no Kind::Vehicle name signatures left, so the WiGLE filter matches "
      "nothing and is doing no work")

# ── the matcher actually reads the field ────────────────────────────────────
#
# Without this the length is decoration: every row above could declare one and
# every one of them would still match on its prefix alone.
check(re.search(r"exactLen\s*==\s*0\s*\|\|", code) is not None,
      "Spotter.cpp no longer honours NameSig.exactLen, so every name "
      "signature matches on its prefix alone again")

# ── the filter is still in Spotter.cpp ──────────────────────────────────────
#
# Cheap, but the whole point of the table check is that something applies it.
check("BLE_ADDR_PUBLIC" in code,
      "Spotter.cpp no longer gates the BLE OUI match on a public address; an "
      "OUI read off a random address is noise")
check(re.search(r"conf\s*!=\s*Conf::Strong", code) is not None,
      "Spotter.cpp no longer filters the BLE OUI match to Strong entries")

checks = (len(entries) * 4 + len(VENDOR_OWN) + 5
          + len(name_entries) * 3 + len(karr) * 2
          + len(SHORT_BUT_DELIBERATE) + 8)
if fail:
    for f in fail:
        sys.stderr.write("FAIL: %s\n" % f)
    sys.exit(1)

print("%d name signatures, %d declaring an exact length"
      % (len(name_entries), len([e for e in name_entries if e[2]])))
print("%d OUI entries, %d reach the BLE path (%s)"
      % (len(entries), len(strong),
         ", ".join("%s %s" % (o, l) for o, l in strong)))
print("%d checks passed" % checks)
