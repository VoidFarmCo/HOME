#!/usr/bin/env python3
"""Route the remaining hand-rolled SPI setup through SpiBus.

These blocks each reconfigured the shared bus inline on feature entry:

    SPI.begin(NRF24_SPI_SCK, NRF24_SPI_MISO, NRF24_SPI_MOSI, NRF24_SPI_SS);
    SPI.setDataMode(SPI_MODE0);
    SPI.setFrequency(10000000);
    SPI.setBitOrder(MSBFIRST);

Two things were wrong with that beyond the missing ownership. The begin() is
a no-op whenever the bus is already running -- SPIClass::begin() returns
early if _spi is set -- so the pin routing it appears to establish may never
happen. And the three setters are global and sticky, which is how a device
ends up running at the previous feature's clock.

SpiBus::claim() does the parts that actually work, in the right order, and
records who holds the bus.

Line-anchored, because these blocks are identical text in five places.
One-shot: re-running will refuse, the anchors no longer match.
"""

import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

# (file, first line, number of lines to replace, substring expected on the
#  first line, replacement text, trailing comment)
BLOCKS = [
    ("bluetooth.cpp", 4934, 4, "SPI.begin(NRF24_SPI_SCK",
     "SpiBus::claim(SpiBus::Dev::Nrf24);"),
    ("bluetooth.cpp", 6435, 4, "SPI.begin(NRF24_SPI_SCK",
     "SpiBus::claim(SpiBus::Dev::Nrf24);"),
    ("bluetooth.cpp", 7269, 4, "SPI.begin(NRF24_SPI_SCK",
     "SpiBus::claim(SpiBus::Dev::Nrf24);"),
    ("bluetooth.cpp", 8146, 4, "SPI.begin(NRF24_SPI_SCK",
     "SpiBus::claim(SpiBus::Dev::Nrf24);"),
    # SD block inside a BLE feature
    ("bluetooth.cpp", 5597, 4, "SPI.begin(SD_SCLK",
     "SpiBus::claim(SpiBus::Dev::Sd);"),
    # esbInitRadioSpi(): same thing wrapped in a board #if / #else / #endif
    ("bluetooth.cpp", 5559, 8, "#if defined(SD_SCLK)",
     "SpiBus::claim(SpiBus::Dev::Nrf24);"),
]


def main():
    files = {}
    for name in {b[0] for b in BLOCKS}:
        raw = (SKETCH / name).read_bytes()
        nl = "\r\n" if b"\r\n" in raw else "\n"
        files[name] = (raw.decode("utf-8", "surrogateescape").split(nl), nl)

    problems = []
    for name, ln, count, want, _ in BLOCKS:
        lines, _ = files[name]
        if ln - 1 >= len(lines) or want not in lines[ln - 1]:
            got = lines[ln - 1].strip() if ln - 1 < len(lines) else "<past EOF>"
            problems.append("%s:%d expected %r, found %r" % (name, ln, want, got[:60]))
    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("anchors stale; nothing written", file=sys.stderr)
        return 1

    for name, ln, count, _, repl in BLOCKS:
        lines, _ = files[name]
        first = lines[ln - 1]
        indent = first[:len(first) - len(first.lstrip())]
        lines[ln - 1] = indent + repl
        for j in range(ln, ln + count - 1):
            lines[j] = None

    for name, (lines, nl) in files.items():
        kept = [l for l in lines if l is not None]
        (SKETCH / name).write_bytes(nl.join(kept).encode("utf-8", "surrogateescape"))

    print("routed %d inline bus-setup blocks through SpiBus" % len(BLOCKS))
    return 0


if __name__ == "__main__":
    sys.exit(main())
