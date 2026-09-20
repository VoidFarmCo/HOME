# Building a Pueo by hand

Wiring a CYD and four modules into a working unit, in the order that makes a
fault easy to find.

**Nothing in this guide has been wired yet.** Step 2 has been done -- a board
was flashed and booted on 2026-09-20, and display, menu and touch work -- but
no module has been soldered to anything, so steps 3 onward remain untested.
The rest is derived from the pin map
in [hardware.md](hardware.md), which `tools/check_pinmap.py` verifies against
the CYD's own wiring, from the CYD schematic, and from module datasheets. The
first unit goes together on 2026-09-20; whatever that gets wrong is a
correction to this file, not a footnote. Treat the order as reasoned and the
timings as untested.

There is no PCB. [pcb-design.md](pcb-design.md) is design input for one, and
it deliberately comes *after* this: a board freezes a pin map that bring-up
can still move.

## Read this part before you buy anything

**The radios do not share the CYD's 3.3 V regulator.** The NRF24L01+PA+LNA
pulls on the order of 115 mA on transmit and the CC1101 around 34 mA, against
a regulator already carrying an ESP32, a backlight and a display controller.
Sharing it browns out the main rail, and a brownout on an ESP32 looks like a
random reboot, a corrupt SD write, or a touch controller that has stopped
answering — that is, like four different bugs rather than one power problem.

Two rails, from one 5 V bus:

```
  5 V ──┬── CYD onboard regulator ── 3.3 V ── ESP32, display, touch, SD
        │
        └── separate buck ────────── +3V3_RF ── CC1101, NRF24, ATGM336H
```

Common ground between them, and **10 µF across the NRF24's supply pins at the
module**, not at the buck. The PA module's current step is fast enough that
the wire between the two is an inductor.

On the bench you can substitute a lab supply set to 3.3 V for the buck. What
you cannot substitute is the separation.

The PN532 runs from 5 V, not from either 3.3 V rail.

## Parts

| | |
|---|---|
| Base | CYD ESP32-2432S028R — 2.8″ ILI9341 240×320, XPT2046 resistive touch, microSD |
| Sub-GHz | CC1101 on an HW-863 breakout, 300–439 MHz, board SMA |
| 2.4 GHz | NRF24L01+PA+LNA, board SMA |
| NFC | PN532 V3 — **SPI mode, DIP CH1=OFF, CH2=ON** |
| GPS | ATGM336H, 9600 baud, IPEX with an active antenna |
| Power | 1S LiPo, TP4056 with protection, MT3608 boost to 5 V, buck for +3V3_RF |

Antennas on both radios before power. A PA module transmitting into an open
SMA is a module you replace.

## Tools

A fine-tipped iron and thin solder; 30 AWG silicone wire for the pad joints;
tweezers; a multimeter with a continuity beep; and magnification. Six of the
ten joints land on pads smaller than the wire you are used to.

## The ten signals

Four reach a header. Six do not, and that is the whole difficulty of this
build.

| Signal | GPIO | Where it lands | |
|---|---|---|---|
| CC1101 CS | 27 | CN1 header | header |
| CC1101 GDO0 (TX) | 22 | P3 header | header |
| CC1101 GDO2 (RX) | 35 | P3 header | header |
| GPS TX → ESP32 | 1 | P1 JST | header |
| VSPI SCK | 18 | microSD slot pin | **solder** |
| VSPI MOSI | 23 | microSD slot pin | **solder** |
| VSPI MISO | 19 | microSD slot pin | **solder** |
| NRF24 CSN | 4 | RGB LED pad | **solder** |
| NRF24 CE | 16 | RGB LED pad | **solder** |
| PN532 SS | 17 | RGB LED pad | **solder** |

Not connected, deliberately: **NRF24 IRQ** (nothing in the tree reads it; the
driver polls) and **GPS RX** (the module only ever talks).

The SPI bus is not brought out anywhere on a CYD. SCK, MOSI and MISO come off
the microSD slot's own pins, which means those three joints have to be good
enough that the card slot still works afterwards — Spotter's capture log and
the wardriver both write to it.

**[verify] Check CN1, P3 and P1 against your own board before you cut a
wire.** CYD revisions differ and the silkscreen is the authority. This is the
single most likely thing in this guide to be wrong for your unit.

## Order of work

The bus is shared, so a bad joint on SCK, MOSI or MISO breaks every device on
it at once. Build outward from the things that can be tested alone.

**1. Power, with nothing else attached.** Bring up the 5 V bus and the
+3V3_RF rail and meter both before anything is connected to them. Confirm
common ground. A supply that is wrong here damages modules later.

**2. Flash the stock firmware and boot the bare CYD.** Display, backlight and
touch all work before you have introduced a single joint of your own. If the
boot screen and menu come up and touch responds, you have a known-good
starting point — and you will want one.

```bash
esptool.py --chip esp32 -p COM7 write_flash 0x0 pueo-0.3.3-merged.bin
```

**3. The three bus lines, then the SD card.** Solder SCK, MOSI and MISO, then
insert a card and confirm it still mounts. Testing the bus with the one
device that was already wired to it isolates your soldering from everything
that follows.

**4. CC1101.** Four wires, three of them to headers, plus power and ground
from +3V3_RF. Then the jamming detector: activity on screen is enough to say
the bus and the chip select both work.

**5. NRF24.** CSN and CE to the RGB LED pads, power from +3V3_RF, and the
10 µF at the module. The channel scanner should show a populated 2.4 GHz
band in any occupied building; a flat sweep means the module is not answering
on the bus.

**6. PN532.** SS to the last RGB LED pad, power from 5 V. **Set the DIP
switches to SPI before wiring it**: CH1=OFF, CH2=ON. In the wrong mode the
module is silent and looks like a bad joint.

**7. GPS.** One signal — module TX to GPIO 1 — plus power from +3V3_RF and an
active antenna with sky view. Give it minutes, not seconds, for a first fix.

Test after each module rather than at the end. Five devices share VSPI; the
failure you are trying to avoid is one intermittent joint that presents as
four unrelated faults.

## Things that look like faults and are not

**USB serial goes dead while a GPS feature is open.** GPIO 1 is UART0's
transmit pin. The firmware hands the pad to the GPS on entry and takes it
back on exit (`gpsPortOpen`/`gpsPortClose` in `gps.cpp`); the console cannot
exist at the same time. It returns when you leave the feature. This is
inherent to the wiring, and the reasoning for choosing that pin over GPIO 3
is in [hardware.md](hardware.md).

**The RGB LED does nothing.** It is gone. GPIO 4, 16 and 17 are the only
contiguous spare pins on this board and three radios needed six lines.

**CC1101 receive cannot be wired backwards.** GDO2 is on GPIO 35, which is
input-only. If you swap GDO0 and GDO2 the transmit path fails rather than
silently half-working, which is a useful accident.

## Things that are faults

**Touch stops responding after a radio is used.** The touch controller reads
MISO on GPIO 39 while everything else reads GPIO 19, and only one pad can
drive that input at a time. `SpiBus` re-points the matrix on every handover.
If this appears, it is a real bug and worth reporting with the feature you
were in — see [spi-bus.md](spi-bus.md).

**Flakiness that follows CC1101 use.** LSatan's driver calls `SPI.end()`
after every register access, which resets the whole peripheral — including
for the SD card and the touch controller that are also on it. Pueo works
around this, but if the symptom returns it points here first.

**Random reboots under transmit.** Power. Go back to the two rails.

## After it works

`tools/check_pinmap.py` will tell you whether the map you built matches the
one the firmware expects, which is worth running once even though it checks
the source rather than your soldering.

The enclosure in [pueo-enclosure.scad](pueo-enclosure.scad) is dimensioned
around these modules and can be printed while you build. Print
[logo-fit-test.scad](logo-fit-test.scad) first — 76 × 56 × 3 mm, about 10 g,
and it tells you whether your printer holds the logo detail before you commit
to a full lid.
