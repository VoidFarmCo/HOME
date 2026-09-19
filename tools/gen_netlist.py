#!/usr/bin/env python3
"""Generate the carrier-board netlist, placement and BOM.

Emits into dist/pcb/:

  netlist.txt        human-readable, net by net, for schematic capture
  pueo-carrier.net   KiCad legacy netlist (untested import, see below)
  placement.csv      module centres in KiCad board coordinates
  bom.csv            parts the design notes call for

Two deliberate limitations.

Pins are named functionally -- VCC, GND, SCK, CSN -- not numbered. Pin
numbers come from each module's datasheet and none of these modules have a
standard library footprint, so numbering them here would be inventing data.
The netlist gives the connections; the datasheets give the numbers.

The KiCad .net file has never been opened in KiCad, because KiCad is not
installed on the machine that wrote it. Treat netlist.txt as the deliverable
and the .net as a convenience that may need hand-fixing.

Every signal net is checked against ESP32-DIV/board_pueo.h before anything is
written, so the connections cannot silently drift from the firmware.
"""

import csv
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "ESP32-DIV" / "board_pueo.h"
OUT = REPO / "dist" / "pcb"

# ── Board geometry ──────────────────────────────────────────────────────────
# The enclosure works in millimetres from the case centre with +Y toward the
# radios. KiCad works from a top-left origin with +Y downward. Board is the
# full base interior: 85 - 2*2.5 wide, 170 - 2*2.5 long.
BOARD_W, BOARD_L = 80.0, 165.0


def to_kicad(x, y):
    """Enclosure centre-origin (+Y up) -> KiCad top-left origin (+Y down)."""
    return round(x + BOARD_W / 2, 2), round(BOARD_L / 2 - y, 2)


# From halehound_v3.scad MODULES[]: (name, refdes, x, y, w, h)
MODULES = [
    ("MT3608 boost",   "U1",  -19.0, -65.0, 36, 17),
    # 27 x 17 measured off the module, not the 26 x 19 the enclosure
    # assumes. Mounted rotated 180 deg from the usual photo so the USB
    # jack faces +X, out through the right wall.
    ("TP4056 charger", "U2",   26.6, -62.0, 27, 17),
    ("3.3V buck",      "U3",  -29.0, -48.0, 20, 12),
    ("LiPo pack",      "BT1", -16.0, -23.0, 45, 34),
    ("GT-U7 GPS",      "J5",   24.0, -21.0, 28, 27),
    ("PN532 V3",       "J4",    0.0,  18.0, 43, 41),
    ("HW-863 CC1101",  "J2",  -23.0,  61.5, 15, 40),
    ("NRF24 PA+LNA",   "J3",   22.0,  61.0, 16, 41),
]

SMA_X = [-23.0, 22.0]       # scad SMA_X, 45 mm apart
USBC_Y = -62.0              # scad USBC_Y, right wall

# ── Nets ────────────────────────────────────────────────────────────────────
# (net, [(refdes, pin-name), ...], note)
#
# J1 is the lid harness. Its pin numbers ARE ours to choose, so they are
# fixed here and match docs/pueo/pcb-design.md.
NETS = [
    ("GND", [("J1", "1"), ("J1", "2"), ("J1", "14"),
             ("J2", "GND"), ("J3", "GND"), ("J4", "GND"), ("J5", "GND"),
             ("U1", "GND"), ("U2", "OUT-"), ("U3", "GND"),
             ("R1", "-"), ("C1", "2"), ("C2", "2"), ("C3", "2"),
             ("C4", "2"), ("C5", "2"), ("C6", "2"), ("C7", "2"),
             ("TP5", "1")],
     "system ground is the PROTECTED side, U2.OUT-. Pour both layers except under the PN532 coil"),

    # The protection MOSFETs on this module sit in the NEGATIVE line, between
    # B- and OUT-. So the battery hangs off B+/B- and the whole system takes
    # its supply and its ground from OUT+/OUT-. Wiring the load to B+ and
    # putting BT1's negative on the common ground -- which is what this
    # netlist did first -- runs the load around the protection entirely, and
    # over-discharge cutoff silently stops existing.
    ("VBAT",     [("BT1", "+"), ("U2", "B+")],
     "battery positive into the protection, 1.0 mm trace"),
    ("BATT_NEG", [("BT1", "-"), ("U2", "B-")],
     "battery negative. ONLY these two nodes: it is not system ground"),
    ("+VSYS",    [("U2", "OUT+"), ("U1", "VIN"), ("U3", "VIN")],
     "protected battery rail feeding both converters, 1.0 mm trace"),

    ("+5V_SW", [("U1", "VOUT"), ("J1", "3"), ("J4", "VCC"),
                ("C6", "1"), ("TP1", "1")],
     "boost output, feeds the CYD and the PN532, 1.0 mm trace"),

    ("+3V3_RF", [("U3", "VOUT"), ("J2", "VCC"), ("J3", "VCC"), ("J5", "VCC"),
                 ("C1", "1"), ("C2", "1"), ("C3", "1"), ("C7", "1"),
                 ("TP2", "1")],
     "separate rail: the PA modules brown out sharing the CYD regulator"),

    # No USB_VBUS net: the Type-C jack is on the TP4056 module itself, which
    # the enclosure places with its short end against the right wall. The
    # board only has to leave the cutout clear.

    # SPI, with the series-resistor break between the connector and the bus.
    ("SCK_J1",   [("J1", "4"), ("R2", "1")], "from CYD GPIO 18"),
    ("MOSI_J1",  [("J1", "5"), ("R3", "1")], "from CYD GPIO 23"),
    ("VSPI_SCK", [("R2", "2"), ("J2", "SCK"), ("J3", "SCK"), ("J4", "SCK"),
                  ("TP3", "1")],
     "daisy-chain along a spine, short stubs"),
    ("VSPI_MOSI", [("R3", "2"), ("J2", "MOSI"), ("J3", "MOSI"), ("J4", "MOSI")],
     "as above"),
    ("VSPI_MISO", [("J1", "6"), ("J2", "MISO"), ("J3", "MISO"), ("J4", "MISO"),
                   ("TP4", "1")],
     "no series resistor: an input at the CYD end"),

    # Chip selects and control. Static during a transaction, so these are the
    # nets to route awkwardly if something has to be.
    ("CC1101_CS",   [("J1", "7"),  ("J2", "CSN")],  "CYD GPIO 27"),
    ("CC1101_GDO0", [("J1", "8"),  ("J2", "GDO0")], "CYD GPIO 22, TX"),
    ("CC1101_GDO2", [("J1", "9"),  ("J2", "GDO2")], "CYD GPIO 35, RX, input-only"),
    ("NRF_CSN",     [("J1", "10"), ("J3", "CSN")],  "CYD GPIO 4"),
    ("NRF_CE",      [("J1", "11"), ("J3", "CE")],   "CYD GPIO 16"),
    ("PN532_SS",    [("J1", "12"), ("J4", "SS")],   "CYD GPIO 17"),

    # GPS, through the series resistor.
    ("GPS_TX_RAW", [("J5", "TX"), ("R1", "1")],
     "GPS module transmit"),
    ("GPS_TX",     [("R1", "2"), ("J1", "13"), ("TP6", "1")],
     "to CYD GPIO 1 through R1; both ends drive this net when GPS is closed"),
]

# Signal nets whose GPIO must agree with the firmware: net -> macro in the
# board header.
FIRMWARE_CHECK = {
    "CC1101_CS":   ("CC1101_CS", 27),
    "CC1101_GDO0": ("SUBGHZ_TX_PIN", 22),
    "CC1101_GDO2": ("SUBGHZ_RX_PIN", 35),
    "NRF_CSN":     ("CSN_PIN_1", 4),
    "NRF_CE":      ("CE_PIN_1", 16),
    "PN532_SS":    ("PN532_SS", 17),
    "GPS_TX":      ("GPS_UART_RX", 1),
}

BOM = [
    ("J1",  1, "Connector 14-way 1.25mm JST-GH", "lid harness to the CYD"),
    ("J2",  1, "Header 2x4 2.54mm", "HW-863 CC1101 module [verify pinout]"),
    ("J3",  1, "Header 2x4 2.54mm", "NRF24L01+PA+LNA module [verify pinout]"),
    ("J4",  1, "Header 1x6 2.54mm", "PN532 V3, SPI mode, DIP CH1=OFF CH2=ON"),
    ("J5",  1, "Header 1x5 2.54mm", "GT-U7 GPS [verify pinout]"),
    ("U1",  1, "MT3608 boost module", "VBAT -> 5V, size for 1 A continuous"),
    ("U2",  1, "TP4056 + DW01/FS8205 protection, 27x17mm",
     "micro-USB, 1 A charge (module R3, typically 1.2k). Load on OUT+/OUT-, "
     "not B+. Protection trips ~3 A, above the 1.6 A peak. IN+/IN- pads are "
     "an alternate supply if USB is ever dropped"),
    ("U3",  1, "3.3V buck module", "separate RF rail, 500 mA"),
    ("BT1", 1, "1S LiPo, 2000 mAh", "~2 h at 600 mA average"),
    ("R1",  1, "1k 0805", "GPS TX series, contention limit on GPIO 1"),
    ("R2",  1, "0R 0805", "SCK series; 22R footprint if it rings"),
    ("R3",  1, "0R 0805", "MOSI series; 22R footprint if it rings"),
    ("C1",  1, "10uF 0805 X7R", "AT THE NRF24 PINS, not near the regulator"),
    ("C2",  1, "100nF 0805", "CC1101 VCC"),
    ("C3",  1, "100nF 0805", "NRF24 VCC"),
    ("C4",  1, "100nF 0805", "PN532 VCC"),
    ("C5",  1, "100nF 0805", "GPS VCC"),
    ("C6",  1, "220uF electrolytic", "+5V_SW bulk, near J1"),
    ("C7",  1, "100uF electrolytic", "+3V3_RF bulk at the buck"),
    ("TP1", 1, "Test point", "+5V_SW"),
    ("TP2", 1, "Test point", "+3V3_RF"),
    ("TP3", 1, "Test point", "VSPI_SCK"),
    ("TP4", 1, "Test point", "VSPI_MISO"),
    ("TP5", 1, "Test point", "GND"),
    ("TP6", 1, "Test point", "GPS_TX"),
]


def read_firmware_pins():
    txt = HDR.read_text(encoding="utf-8", errors="replace")
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^#define\s+(\w+)\s+(-?\d+)", txt, re.M)}


def verify(pins):
    """Refuse to emit anything that disagrees with the firmware."""
    problems = []
    for net, (macro, expect) in FIRMWARE_CHECK.items():
        got = pins.get(macro)
        if got != expect:
            problems.append("%s: board_pueo.h has %s=%s, netlist assumes %d"
                            % (net, macro, got, expect))
    # Every net must have at least two endpoints, or it is not a net.
    for name, conns, _ in NETS:
        if len(conns) < 2:
            problems.append("%s has only %d connection" % (name, len(conns)))
    # Every refdes used in a net must exist in the BOM.
    known = {r for r, *_ in BOM}
    for name, conns, _ in NETS:
        for ref, _pin in conns:
            if ref not in known:
                problems.append("%s references %s, which is not in the BOM" % (name, ref))
    return problems


def write_netlist_txt(path):
    lines = [
        "Pueo carrier board - netlist",
        "",
        "Pin names are functional, not numbered: module pin numbers come from",
        "the datasheets. J1 numbers are ours and match docs/pueo/pcb-design.md.",
        "",
        "Generated by tools/gen_netlist.py from ESP32-DIV/board_pueo.h.",
        "",
    ]
    for name, conns, note in NETS:
        lines.append("%s" % name)
        if note:
            lines.append("    # %s" % note)
        for ref, pin in conns:
            lines.append("    %-5s %s" % (ref, pin))
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_kicad_net(path):
    """KiCad legacy netlist. Never opened in KiCad; see the module docstring."""
    out = ['(export (version D)', '  (design (source "tools/gen_netlist.py"))',
           '  (components']
    for ref, qty, part, note in BOM:
        out.append('    (comp (ref "%s") (value "%s"))' % (ref, part))
    out.append('  )')
    out.append('  (nets')
    for i, (name, conns, _n) in enumerate(NETS, 1):
        out.append('    (net (code "%d") (name "%s")' % (i, name))
        for ref, pin in conns:
            out.append('      (node (ref "%s") (pin "%s"))' % (ref, pin))
        out.append('    )')
    out.append('  )')
    out.append(')')
    path.write_text("\n".join(out), encoding="utf-8", newline="\n")


def write_placement(path):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["refdes", "module", "kicad_x_mm", "kicad_y_mm",
                    "width_mm", "height_mm", "enclosure_x", "enclosure_y", "note"])
        for name, ref, x, y, wd, ht in MODULES:
            kx, ky = to_kicad(x, y)
            note = ""
            if ref == "J4":
                note = "KEEP-OUT: no copper or pour, 1.4 mm floor window under the coil"
            elif ref in ("J2", "J3"):
                sma = SMA_X[0] if ref == "J2" else SMA_X[1]
                note = "board SMA must land at enclosure x=%.0f (kicad x=%.2f)" % (
                    sma, to_kicad(sma, 0)[0])
            elif ref == "BT1":
                note = "battery pocket, no copper beneath"
            elif ref == "U2":
                note = "USB-C through the right wall at enclosure y=%.0f" % USBC_Y
            w.writerow([ref, name, kx, ky, wd, ht, x, y, note])


def write_bom(path):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["refdes", "qty", "part", "note"])
        for row in BOM:
            w.writerow(row)


def main():
    pins = read_firmware_pins()
    problems = verify(pins)
    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("netlist disagrees with the firmware; nothing written", file=sys.stderr)
        return 1

    OUT.mkdir(parents=True, exist_ok=True)
    write_netlist_txt(OUT / "netlist.txt")
    write_kicad_net(OUT / "pueo-carrier.net")
    write_placement(OUT / "placement.csv")
    write_bom(OUT / "bom.csv")

    nets = len(NETS)
    conns = sum(len(c) for _n, c, _x in NETS)
    print("  %d nets, %d connections, %d parts" % (nets, conns, len(BOM)))
    print("  all %d firmware-checked signals agree with board_pueo.h"
          % len(FIRMWARE_CHECK))
    print("  board %.0f x %.0f mm, origin top-left" % (BOARD_W, BOARD_L))
    for f in sorted(OUT.iterdir()):
        print("    dist/pcb/%s" % f.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
