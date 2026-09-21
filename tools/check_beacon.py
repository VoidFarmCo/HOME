#!/usr/bin/env python3
"""The bench beacon emits things the detector actually looks for.

A decoy is only worth having if it targets a real signature. Emit a plate
reader on an OUI Spotter does not carry, or a Fast Pair payload under the
wrong UUID, and the bench goes quiet -- which looks exactly like a broken
receiver, and sends you debugging the half that was fine.

The other direction is worse and quieter. If the beacon carried its own copy
of the constants, the two would drift: the detector's table gets an entry
corrected, the decoy keeps emitting the old one, and a green bench now means
nothing at all. So Emit.cpp includes the detector's headers rather than
restating them, and the first thing checked here is that it still does.

    python tools/check_beacon.py

Reads source; needs neither board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"
BEACON = REPO / "PueoBeacon"

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


def oui_table(src):
    """kOuiSigs -> {(b0,b1,b2): (kind, label)}"""
    out = {}
    body = src[src.index("kOuiSigs[]"):]
    body = body[:body.index("};")]
    for m in re.finditer(
            r"\{\{(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2})\}\s*,"
            r"\s*Kind::(\w+)\s*,\s*Conf::(\w+)\s*,\s*\"([^\"]*)\"", body):
        key = tuple(int(m.group(i), 16) for i in (1, 2, 3))
        out[key] = (m.group(4), m.group(5), m.group(6))
    return out


def ble_table(src):
    """kBleSigs -> {(company, service): (kind, conf, label)}"""
    out = {}
    body = src[src.index("kBleSigs[]"):]
    body = body[:body.index("};")]
    for m in re.finditer(
            r"\{(0x[0-9A-Fa-f]{4})\s*,\s*(0x[0-9A-Fa-f]{4})\s*,"
            r"\s*Kind::(\w+)\s*,\s*Conf::(\w+)\s*,\s*\"([^\"]*)\"", body):
        out[(int(m.group(1), 16), int(m.group(2), 16))] = (
            m.group(3), m.group(4), m.group(5))
    return out


def name_table(src):
    """kBleNameSigs -> [(prefix, exactLen, kind, conf, label)]

    The BLE table, not kSsidSigs: the vehicle decoy advertises a BLE
    Complete Local Name, so that is the table that has to carry it."""
    out = []
    body = src[src.index("kBleNameSigs[]"):]
    body = body[:body.index("};")]
    for m in re.finditer(
            r"\{\"([^\"]*)\"\s*,\s*(\d+)\s*,\s*Kind::(\w+)\s*,"
            r"\s*Conf::(\w+)\s*,\s*\"([^\"]*)\"", body):
        out.append((m.group(1), int(m.group(2)), m.group(3), m.group(4), m.group(5)))
    return out


def main():
    emit = (BEACON / "Emit.cpp").read_text(encoding="utf-8", errors="replace")
    emit_h = (BEACON / "Emit.h").read_text(encoding="utf-8", errors="replace")
    ino = (BEACON / "PueoBeacon.ino").read_text(encoding="utf-8", errors="replace")
    sigs = (SKETCH / "SpotterSignatures.h").read_text(encoding="utf-8", errors="replace")

    print("the beacon reads the detector's constants, it does not restate them:")
    for h in ("DroneId.h", "FastPair.h", "SpotterSignatures.h"):
        ok("Emit.cpp includes %s" % h, ('#include "%s"' % h) in emit)
    # The failure this guards: a copy that drifts. If any of these appear as
    # literals in the beacon, it has its own opinion about the wire format.
    for bad, why in (
            (r"0xFFFA", "the Remote ID service UUID"),
            (r"0xFE2C", "the Fast Pair UUID"),
            (r"0xFA,\s*0x0B,\s*0xBC", "the ASD-STAN OUI"),
            (r"\b25\b\s*;", "the Remote ID message size")):
        ok("no literal copy of %s" % why,
           re.search(bad, emit) is None,
           "found one in Emit.cpp")

    print("\nevery emitted signal targets a signature the detector carries:")
    ouis = oui_table(sigs)
    bles = ble_table(sigs)
    names = name_table(sigs)

    m = re.search(r"s_alprMac\[6\]\s*=\s*\{(0x[0-9A-Fa-f]{2}),\s*"
                  r"(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2})", emit)
    ok("the ALPR decoy uses an OUI Spotter carries", m is not None)
    if m:
        key = tuple(int(m.group(i), 16) for i in (1, 2, 3))
        hit = ouis.get(key)
        ok("  and it is classified Alpr", hit is not None and hit[0] == "Alpr",
           "%02X:%02X:%02X -> %s" % (key + (hit,)))
        ok("  on a Strong signature", hit is not None and hit[1] == "Strong",
           str(hit))

    m = re.search(r"s_bodycamMac\[6\]\s*=\s*\{(0x[0-9A-Fa-f]{2}),\s*"
                  r"(0x[0-9A-Fa-f]{2}),\s*(0x[0-9A-Fa-f]{2})", emit)
    ok("the bodycam decoy uses an OUI Spotter carries", m is not None)
    if m:
        key = tuple(int(m.group(i), 16) for i in (1, 2, 3))
        hit = ouis.get(key)
        ok("  and it is classified Bodycam",
           hit is not None and hit[0] == "Bodycam",
           "%02X:%02X:%02X -> %s" % (key + (hit,)))

    m = re.search(r"uuid16\((0x[0-9A-Fa-f]{4})\)\s*\+\s*mfgData\((0x[0-9A-Fa-f]{4})",
                  emit)
    ok("the glasses decoy advertises a company/service pair", m is not None)
    if m:
        service, company = int(m.group(1), 16), int(m.group(2), 16)
        hit = bles.get((company, service))
        ok("  and the pair is in kBleSigs as Glasses",
           hit is not None and hit[0] == "Glasses",
           "company %04X service %04X -> %s" % (company, service, hit))
        ok("  on a Strong signature", hit is not None and hit[1] == "Strong",
           str(hit))

    m = re.search(r'completeName\("([^"]*)"\)', emit)
    ok("the vehicle decoy advertises a name", m is not None)
    if m:
        nm = m.group(1)
        hits = [s for s in names
                if nm.upper().startswith(s[0].upper())
                and (s[1] == 0 or len(nm) == s[1])]
        ok("  and it matches a kBleNameSigs entry", bool(hits),
           "%r matched nothing" % nm)
        ok("  classified Vehicle", bool(hits) and hits[0][2] == "Vehicle",
           str(hits[:1]))

    print("\nthe scheduler actually sends everything it declares:")
    # `= 0` on the first enumerator is easy to leave out of this pattern, and
    # doing so drops RemoteIdWifi silently -- the check then reports green
    # over a signal nobody sends. It did exactly that once.
    declared = re.findall(r"^\s*(\w+)\s*(?:=\s*\w+\s*)?,",
                          emit_h[emit_h.index("enum Signal"):
                                 emit_h.index("kSignalCount")], re.M)
    declared = [d for d in declared if d not in ("kSignalCount",)]
    ok("found the signal list", len(declared) >= 8, str(declared))
    scheduled = set(re.findall(r"Emit::(\w+)", ino))
    for d in declared:
        ok("%s is scheduled" % d, d in scheduled,
           "declared in Emit.h and never sent")

    print("\nit is built to be a bench instrument:")
    ok("transmit power is set to a floor",
       "ESP_PWR_LVL_N12" in emit and "esp_wifi_set_max_tx_power" in emit)
    ok("there is an auto-stop", "kAutoStopMs" in emit_h and "allStop" in ino)
    ok("the auto-stop is wired to it",
       re.search(r"s_started\s*\)\s*>=\s*Emit::kAutoStopMs", ino) is not None)
    ok("payloads identify themselves as a test",
       emit.count("PUEO-TEST") >= 3, "%d mentions" % emit.count("PUEO-TEST"))
    ok("the screen says it is transmitting", "TRANSMITTING" in ino)

    # The one that cost an evening. The detector carries this override in
    # wifi.cpp and the beacon is a separate sketch, so it did not inherit it:
    # every hand-built frame was refused by the IDF before it reached the air,
    # while BLE sailed through. A bench that finds the glasses and never the
    # plate reader looks like two broken detectors, not one missing function.
    ok("the beacon overrides the raw-frame sanity check",
       "ieee80211_raw_frame_sanity_check" in emit,
       "without it esp_wifi_80211_tx refuses every hand-built frame")
    ok("and it checks whether the frame was accepted",
       "const esp_err_t r = esp_wifi_80211_tx" in emit,
       "a discarded return makes a dead transmitter look like a dead receiver")
    ok("a refusal is shown on the screen", "TX REFUSED" in ino)

    print("\nand it is a separate image from the detector:")
    build = (REPO / "tools" / "build.sh").read_text(encoding="utf-8",
                                                    errors="replace")
    ok("build.sh knows both roles",
       'PUEO_ROLE' in build and 'SKETCH_DIR="PueoBeacon"' in build)
    ok("the detector does not compile the emitter",
       not (SKETCH / "Emit.cpp").exists())
    ok("and the beacon is not in the detector's sketch directory",
       not list(SKETCH.glob("PueoBeacon*")))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
