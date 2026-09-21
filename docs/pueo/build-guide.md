# Building a Pueo by hand

Wiring a CYD and four modules into a working unit, in the order that makes a
fault easy to find.

**Nothing in this guide has been wired yet.** Step 2 is done -- a board was
flashed and run on 2026-09-20, and the display, the menus, touch, the WiFi
scanner, the packet monitor, Spotter and Hunt all work -- but no module has
been soldered to anything, so steps 3 onward remain untested. The rest is
derived from the pin map in [hardware.md](hardware.md), which
`tools/check_pinmap.py` verifies against each board's own wiring, from the
CYD schematic, and from module datasheets. Treat the order as reasoned and
the timings as untested.

**Which board you have changes where two wires go.** Step 2 found that the
2.8" and the 3.5" do not agree about GPIO 4, and the pin map now follows the
panel. Every place that matters is marked below; if you are building on a
3.5", read those before you cut anything.

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
| Base | CYD, either panel — see below |
| Sub-GHz | CC1101 on an HW-863 breakout, 300–439 MHz, board SMA |
| 2.4 GHz | NRF24L01+PA+LNA, board SMA |
| NFC | PN532 V3 — **SPI mode, DIP CH1=OFF, CH2=ON** |
| GPS | ATGM336H, 9600 baud, IPEX with an active antenna |
| Power | 1S LiPo, TP4056 with protection, MT3608 boost to 5 V, buck for +3V3_RF |

Antennas on both radios before power. A PA module transmitting into an open
SMA is a module you replace.

### "Cheap yellow display" names at least four boards

Two vendors, and they do not agree about GPIO 4. Get this wrong and the
NRF24's chip select lands on an audio amplifier.

| | Sunton **ESP32-2432S028R** | lcdwiki **E32R28T** | lcdwiki **E32R35T** |
|---|---|---|---|
| Size | 2.8" | 2.8" | 3.5" |
| Driver | ILI9341 | ILI9341V | ST7796 |
| RGB LED | **4** / 16 / 17 | **22** / 16 / 17 | **22** / 16 / 17 |
| GPIO 4 is | the LED's red | **amp enable** | **amp enable** |
| Outline | ~56 × 92.5 mm | 50.00 × 86.00 | 55.50 × 101.50 |
| Serial hdr | P5 | 1.25 mm JST | UART JST |

**Pueo's 2.8" build targets the Sunton board**, where GPIO 4 is the LED's
red channel and spending it is the trade this pin map makes. On any lcdwiki
E32 board that pin is an amplifier's enable, and the 2.8" image would key it
at chip-select rates. The 3.5" image already moves CSN to GPIO 25 for
exactly that reason; the 2.8" image does not, because the board it is for
does not need it to.

**The lcdwiki 4.0" E32R40T should run the 3.5" image unchanged.** Every pin
lcdwiki lists for it is the same as the E32R35T's -- display SPI on
14/13/12 with CS 15 and DC 2, touch on that bus behind CS 33 with IRQ 36,
backlight 27, SD on 5/18/23/19, RGB on 22/16/17, amplifier on 4 and 26,
battery on 34 -- and the resolution is the same 320x480. The only entry
that differs at all is the controller's suffix, ST7796S against the 3.5"'s
ST7796U, and TFT_eSPI drives the family with one `ST7796_DRIVER`.

So the firmware needs nothing: flash `pueo-<version>-35-merged.bin`. The
name is then wrong about the diagonal and right about everything it
actually selects.

What does not carry over is the enclosure. The 4.0" is 60.88 x 111.11 x
5.65 mm against the 3.5"'s 55.50 x 101.50 x 5.80, and there is no outline
drawing here for it -- no aperture, no module outline, no mounting-hole
pattern -- so a lid for it would be guesswork. [verify] and measure before
cutting one.

None of this has been on a 4.0" board. It is two datasheets agreeing, which
is a good reason to expect it to work and not the same thing as it working.

If your 2.8" board's silkscreen reads `ESP32 2432S028`, it is the Sunton and
the 2.8" column below is for you. If it reads `E32R28T`, it is not, and
nothing in this tree has been built for it.

### The two panels this tree builds for

Both are sold as a "cheap yellow display" and they are not the same board.

| | 2.8″ ESP32-2432S028R | 3.5″ ESP32-3248S035R |
|---|---|---|
| Display | ILI9341, 240×320 | ST7796, 320×480 |
| Touch | XPT2046 on its own bus, 25/32/39 | XPT2046 on the display's SPI, CS 33 |
| Backlight | GPIO 21 | GPIO 27 |
| RGB LED | 4 / 16 / 17 | **22** / 16 / 17 |
| GPIO 4 is | the LED's red channel | **the audio amplifier's enable** |
| GPIO 34 is | an LDR | the battery divider |
| Flash image | `pueo-<ver>-merged.bin` | `pueo-<ver>-35-merged.bin` |

The 3.5" figures are from lcdwiki's E32R35T page and QDtech's outline drawing
(V1.0, 2024-08-14): PCB 55.50 × 101.50 × 5.80 mm, corners R3.50, four 3.20 mm
mounting holes on a 47.90 × 94.50 pattern, 5.09 mm of SMD standing off the
back. The enclosure is dimensioned from those numbers.

There is no runtime detection. The image is built for one panel and the wrong
one is a dark screen rather than an error message.

## Tools

A fine-tipped iron and thin solder; 30 AWG silicone wire for the pad joints;
tweezers; a multimeter with a continuity beep; and magnification. Six of the
ten joints land on pads smaller than the wire you are used to.

## The ten signals

**How many of them you have to solder depends on which board you have**, and
the difference is large: three joints on the 3.5", six on the 2.8".

| Signal | GPIO | 2.8" ESP32-2432S028R | 3.5" ESP32-3248S035R |
|---|---|---|---|
| CC1101 CS | 27 / 21 | CN1, header | **SPI** JST, `IO21(CS)` |
| CC1101 GDO2 (RX) | 35 | P3, header | **IO35/IO39** JST |
| GPS TX → ESP32 | 1 | **P5** JST, `TX` | **UART** JST, `TXD` |
| VSPI SCK | 18 | microSD pin, **solder** | **SPI** JST, `IO18(SCK)` |
| VSPI MOSI | 23 | microSD pin, **solder** | **SPI** JST, `IO23(MOSI)` |
| VSPI MISO | 19 | microSD pin, **solder** | **SPI** JST, `IO19(MISO)` |
| NRF24 CSN | 4 / 25 | RGB LED pad, **solder** | **I2C** JST, `IO25(SCL)` |
| CC1101 GDO0 (TX) | 22 | P3, header | RGB LED pad, **solder** |
| NRF24 CE | 16 | RGB LED pad, **solder** | RGB LED pad, **solder** |
| PN532 SS | 17 | RGB LED pad, **solder** | RGB LED pad, **solder** |

Not connected, deliberately: **NRF24 IRQ** (nothing in the tree reads it; the
driver polls) and **GPS RX** (the module only ever talks).

**On the 2.8", the SPI bus is not brought out anywhere.** SCK, MOSI and MISO
come off the microSD slot's own pins, which means those three joints have to
be good enough that the card slot still works afterwards — Spotter's capture
log and the wardriver both write to it. That is the whole difficulty of the
build on that board.

**On the 3.5" it is brought out, on a four-pin JST.** The silkscreen labels
it `SPI` and lists `IO23(MOSI) IO19(MISO) IO18(SCK) IO21(CS)` — which is the
whole VSPI bus plus CC1101's chip select, on a connector. The `I2C` header
next to it carries `3V3 IO32(SDA) IO25(SCL) GND`, and IO25 is where NRF24's
chip select goes on this panel, so that comes off a connector too, with 3.3 V
and ground beside it.

What is left to solder on the 3.5" is the three RGB LED pads: GDO0, CE and
the PN532's select. The LED is gone either way — that is what those pins were
spent on — so the joints land on pads nothing else wants.

**[verify] Read your own silkscreen before you cut a wire.** Neither column
above came from a board under a meter -- the 3.5" is off lcdwiki's render and
the 2.8" is off a product photo -- and CYD revisions differ. The silkscreen is
the authority and it is printed next to each connector.

The 2.8"'s serial connector is **P5** -- a four-pin JST carrying
`GND TX RX VIN`. This guide called it P1 until someone read it off a board.
The same board shows no SPI breakout anywhere, which is the claim above
about the microSD pins holding up rather than being an assumption.

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
esptool.py --chip esp32 -p COM7 write_flash 0x0 pueo-0.3.4-merged.bin
```

On a 3.5" board flash `pueo-0.3.4-35-merged.bin` instead. If the screen stays
dark, that is the first thing to check -- it is what the wrong image looks
like, and it is not a soldering fault because you have not soldered anything
yet.

**3. The three bus lines, then the SD card.** On the 2.8", solder SCK, MOSI
and MISO to the microSD slot pins; on the 3.5", plug into the `SPI` header
instead. Either way, insert a card and confirm it still mounts. Testing the
bus with the one device that was already wired to it isolates your work from
everything that follows, and on the 2.8" it is specifically testing that
three joints on a card slot did not kill the card slot.

**4. CC1101.** Four wires, three of them to headers, plus power and ground
from +3V3_RF. Then the jamming detector: activity on screen is enough to say
the bus and the chip select both work.

**[verify] On a 3.5" board, CSN is GPIO 25, not GPIO 4.** GPIO 4 is the
audio amplifier's enable on that panel and driving it as a chip select clicks
the speaker. The firmware already picks 25 there; what this guide cannot do is
solder the wire to the right pad for you. 25 is not on a header -- it is one of
the pins the 2.8" uses for its separate touch bus, so on the 3.5" it is free but
still needs finding on the silkscreen.

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

**The RGB LED does nothing.** It is gone. On the 2.8" that is GPIO 4, 16 and 17;
on the 3.5" the red channel is GPIO 22 instead of 4, and CC1101's GDO0 lands on
it, so on that board the red LED flickers with sub-GHz traffic rather than going
dark.

The LED is spent because those were the only contiguous spare pins on the
board and three radios needed six lines. On the 3.5" the amplifier on GPIO 4
is *not* spent the same way: NRF24 CSN moved to 25 rather than the pin map
growing to cover it, because keying an amplifier at chip-select rates is not
the same kind of trade as losing an LED.

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
