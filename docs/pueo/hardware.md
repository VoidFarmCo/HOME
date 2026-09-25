# Pueo hardware

A CYD with four external peripherals, in a custom 84 x 142 x 20 enclosure --
85 x 170 if your board has no onboard charger and the power stack has to ride
inside. See [pueo-enclosure.scad](pueo-enclosure.scad).

The reference build is the 3.5" Sunton ESP32-3248S035R, silkscreened
`ESP32-035`, on a revision carrying **both micro-USB and USB-C**.

For the order to wire it in, and which six of the ten signals need soldering
rather than a header, see [build-guide.md](build-guide.md).

## Connectors

Read off the board, connector by connector, on 2026-09-23.

| | Pins | Vendor's name for it | Signals |
|---|---|---|---|
| **P3** | 4 | Extended IO | `GND IO35 IO22 IO21` -- all three CC1101 control lines |
| **CN1** | 4 | temperature/humidity (DHT11) | `GND IO22 IO21 3.3V` |
| **P1** | 4 | "4P 1.25 Power supply base" | `5V TX RX GND` -- serial, and the GPS's TX |
| **SPEAK1** | 2 | Speak | speaker, GPIO 26 |
| **BAT1** | 2 | *(not in the datasheet)* | battery, into the onboard charger |

The middle column comes from Sunton's own datasheet -- *3.5 inch ESP32
module ESP32-3248S035R/C*, Shenzhen Jingcai Intelligent, six pages, with a
labelled render of the board on page 5. It explains CN1, which this file
listed as four pins with no stated purpose: `GND IO22 IO21 3.3V` is a
four-pin DHT11 header, which is why there is a 3.3 V pin on it at all.

**The four-pin connectors are 1.25 mm pitch.** P1 carries the vendor's own
"4P 1.25" label, and a photogrammetric check of CN1 agreed with it
independently.

### BAT1 is not one of them

This file used to say "all of them are 1.25 mm pitch" and close with *Sunton's
own documentation puts SPEAK and the battery connector at 1.25 as well.*
That sentence was wrong twice. The datasheet's render shows six connectors
and **BAT1 is not among them** -- nor is the USB-C socket -- so it documents
an earlier revision than the board in hand and says nothing about either.
And the pitch was never measured on a two-pin connector at all.

Measured on 2026-09-24 against P1 in the same frame, which needs no scale bar
because it is a ratio: P1's four pins fall at y = 852 / 911 / 971 / 1031 for a
pitch of 60.0 px against a known 1.25 mm, and BAT1's two pads at x = 442 / 507
for 65.5 px, or 1.36 mm.

That **excludes JST PH 2.0**, which would have measured 96 px, and that is the
exclusion that matters in practice because PH is what most hobby cells ship
with. It does not separate 1.25 (+9%) from 1.50 (-9%), and a 9% scale gradient
across 500 px of a macro shot is ordinary perspective. Settle it by fit: if a
2-pin MX1.25 housing seats, it is 1.25.

**Buy MX1.25 or Molex PicoBlade, never JST GH.** Both are 1.25 mm pitch and
they do not mate -- GH latches on the side, PicoBlade on top. GH is the
Pixhawk standard, so listings aimed at drone builders are full of it, and
several of them say "PicoBlade" and "for Pixhawk" in the same title.

**BAT1's polarity is on the silkscreen, and only half of it. The half that
is there is right.** `BAT1` sits to the left of the connector and `BAT-` to
the right, so the pin nearer the corner mounting screw is negative and the
other one is positive.

Measured on 2026-09-25 against CN1's GND pin with a continuity beep: the pin
under the `BAT-` marking is the one that conducts. The silkscreen and the
meter agree, which is worth saying because four other things this board was
supposed to be turned out to describe somebody else's hardware.

Check it on your own board anyway. The FM5324GA has no reverse protection on
its cell input, so the cost of being wrong is the charger, and a label three
millimetres from the pin it refers to is exactly the kind of thing that reads
one way in a photograph and the other way on the bench.

### What else the datasheet is good for, and where it is not

Two things in it are worth keeping. It gives the mounting holes as 94.5 x
47.9, which is a second source for the `47.90 x 94.50` the enclosure's
`SCREEN_HOLE_DX/DY` are built on. And its render has no I2C or SPI JST on it
either, which is independent support for those having been lcdwiki's rather
than this board's.

Two things in it are not. It gives the module as 101.5 x 54.9 where the
dimensioned drawing the enclosure uses says 55.50 wide, which would have made
`BEZEL_W`'s 56.0 a 1.1 mm clearance rather than the 0.5 its comment claims.
**Settled on 2026-09-24 by printing the lid: the board fits it.** Looser, not
tighter, so nothing bound -- and that is why a drawing of the wrong board
(QDtech's E32R35T, lcdwiki's, not Sunton's) still produced a part that works.

And page 3 says "The display resolution is 240x320" in prose and "320X480
resolution" in the feature list four lines below it. It is the manufacturer,
but it is not careful, and it is copy-pasted from the smaller panel's
document. Weigh it accordingly.

**The serial header is P1 on this panel.** The 2.8" calls the same thing P5,
and this document said P5 for both after someone read that off a 2.8" board.

Its pins run **5V, TX, RX, GND** with 5V nearest the corner mounting hole and
GND furthest from it. That order matters more than the inventory does: the
GPS's transmit line goes to the pin marked `TX`, which is the ESP32's own
UART0 transmit, GPIO 1. Reversing it puts two push-pull drivers on one net.
See "Why GPIO 1 for GPS" below for why that assignment is deliberate.

There is no `SPI` and no `I2C` JST. Those belong to lcdwiki's E32R35T, which
is where most of what this file used to say about the 3.5" came from.

## Pin map

Machine-checked by `tools/check_pinmap.py`, which resolves the macros the way
the compiler will and cross-references them against the CYD's own wiring.

| Signal | GPIO | Notes |
|---|---|---|
| VSPI SCK | 18 | shared bus |
| VSPI MOSI | 23 | shared bus |
| VSPI MISO | 19 | shared bus |
| SD CS | 5 | onboard slot |
| CC1101 CS | **21 / 27** | panel-dependent, see below |
| CC1101 GDO0 (TX) | 22 | P3 header |
| CC1101 GDO2 (RX) | 35 | P3 header, input-only pin |
| NRF24 CSN | 4 / 25 | was RGB LED red (2.8"); 25 on the 3.5", see below |
| NRF24 CE | 16 | was RGB LED green |
| NRF24 IRQ | not connected | see below |
| PN532 SS | 17 | was RGB LED blue, SPI mode (DIP CH1=OFF, CH2=ON) |
| GPS TX -> ESP32 | 1 | UART0 TX, the `TX` pin on P1 (P5 on the 2.8") |
| GPS RX | not connected | |

**Which 2.8" board you have matters more than it should.** "Cheap yellow
display" names boards from at least two vendors. This tree targets Sunton's
**ESP32-2432S028R**, whose silkscreen reads `ESP32 2432S028`. lcdwiki's
2.8" is the **E32R28T** -- a different board, 50.00 x 86.00 mm against the
Sunton's larger outline, and it puts the RGB LED's red channel on GPIO 22
and an audio amplifier's enable on GPIO 4. Nothing here has been built for
it, and the 2.8" image would drive that amplifier as a chip select.

**Both Sunton boards agree about GPIO 4: it is the RGB LED's red channel.**
That was measured on 2026-09-23, by driving each candidate low in turn -- the
LED is common anode, so a pin sinks its own channel -- and watching which
colour came up. GPIO 4 red, 16 blue, 17 green, and 22 nothing at all.

Until then this section said GPIO 4 was an **audio amplifier's enable** on the
3.5" with RGB red moved to 22. That is true of lcdwiki's E32R35T and of
nothing in this build. The pin map had been following a datasheet for a board
nobody here owns.

It cost less than it might have. NRF24 CSN follows the panel, 4 on the 2.8"
and **25 on the 3.5"**, and the reason given for the split -- that keying an
amplifier enable at chip-select rates clicks and draws off the display's rail
-- was about a hazard that is not there. CSN stays on 25 anyway: it is free on
that panel, the split is published and built against, and the case for
reverting rests on one measurement of one board, which is the same weight of
evidence that put the amplifier there to begin with.

25 is free there for the same reason it is not free on the
2.8": that panel puts touch on its own bus at 25/32/39, while the 3.5" hangs its
XPT2046 off the display's SPI behind TOUCH_CS. The pin that collides on one
board is the spare on the other.

The 2.8" is left alone deliberately. A published pin map is a thing people have
already soldered to.

Two more pins on the 3.5", neither of which Pueo drives: GPIO 34 is a CdS
light sensor, as it is on the 2.8", and GPIO 36 is the touch IRQ.

**The lcdwiki 4.0" E32R40T is pin-identical to the 3.5"** on every line
lcdwiki publishes, at the same 320x480, differing only in the controller's
suffix -- which TFT_eSPI covers with one driver. The 3.5" image should run
on it unchanged. Untried here: two datasheets agreeing is a reason to
expect it to work, not a report that it did. GPIO 26 is the
amplifier's DAC output rather than a plain speaker pin.

The onboard RGB LED is gone on both. GPIO 4/16/17 are the only contiguous spare pins on
this board, and three radios need six lines.

## CC1101 CS is the one pin the two panels cannot share

The backlight moves between them: GPIO 21 on the 2.8" ESP32-2432S028R, GPIO 27
on the 3.5" ESP32-3248S035R. Whichever one the display is not using is the one
free for a chip select, so `CC1101_CS` is 27 on the 2.8" and 21 on the 3.5",
selected by `PUEO_PANEL_35` in `board_pueo.h`.

On the 3.5" that is a small bonus: 21 is on the Expand IO header (P3: GND,
IO35, IO22, IO21) beside GDO0 and GDO2, so all three CC1101 control lines reach
a connector instead of a pad. On the 2.8" board 27 is on CN1, which is also a
header, so the count of soldered joints is the same either way.

`tools/check_pinmap.py` reads the backlight pin out of `User_Setup cyd.h`
rather than assuming it, so putting the select on the wrong one fails the
build.

That was only half the job, and the other half went missing for the whole
life of the 3.5" port. `User_Setup cyd.h` branches `TFT_BL` per panel, 21
and 27, and TFT_eSPI drives it HIGH at `begin()`. But `shared.h` also has
`BACKLIGHT_PIN`, which the sketch attaches a PWM channel to for the
Brightness setting, and it kept the `BOARD_CYD` default of 21 on both
panels. `board_pueo.h` argued in a comment that 27 was the backlight on the
3.5" -- and moved `CC1101_CS` onto 21 on that basis -- without ever saying
it in code.

So on the 3.5" build, `BACKLIGHT_PIN` and `CC1101_CS` were both GPIO 21, and
this script printed "no collisions" because it read `TFT_BL` and never
compared it to the pin the sketch drives. Neither half failed loudly:
TFT_eSPI lit the real pin so the screen worked, and nothing was soldered to
CC1101 so its select never toggled. The only symptom was Brightness doing
nothing, which is what PWM into an unconnected pad looks like. It now checks
`BACKLIGHT_PIN == TFT_BL` for each panel.

## Why GPIO 1 for GPS

It looks backwards: GPIO 1 is the ESP32's UART0 *transmit* pin, and the GPS is
also transmitting. The alternative is worse.

GPIO 3 (UART0 RX) is driven by the USB-UART bridge's TX output. Tying the GPS
module's TX there puts two push-pull drivers on one net, and they will fight
whenever the bridge is enumerated. GPIO 1 runs the other way: the ESP32 drives
it and the bridge's RX merely listens, so the bridge never contends. Once the
ESP32 releases the pad, the GPS is the only driver on the net.

Releasing it is the part that needs firmware. `gpsPortOpen()` in `gps.cpp`
tears down UART0, resets the pad to a plain input, and only then lets UART2
claim GPIO 1 as its RX. `gpsPortClose()` puts the console back. Every
`gpsSerial` begin/end in the tree goes through that pair, so the handover
cannot be skipped at one call site and silently half-work.

**USB serial logging is dead while a GPS feature is open.** That is inherent to
this wiring, not a bug. It comes back when you leave the feature.

## NRF24 IRQ and the GPIO 17 question

The handoff listed NRF24 IRQ and PN532 SS both on GPIO 17. Resolved in favour
of the PN532: upstream never reads the NRF24 interrupt line. There is no IRQ
pin macro anywhere in the tree and no `whatHappened()` or `maskIRQ()` call; the
NRF24 path is polled throughout. Leave the pin off the board.

## One NRF24, three radio objects

The multi-channel BLE jammer modes were written around three radio objects;
since 0.2.2 they go through `Nrf24Raw`'s free functions instead. Pueo has one
module, so `CE_PIN_2`/`_3` and `CSN_PIN_2`/`_3` are aliased onto the same pads
as `_1`. Those modes will run
degraded on a single radio rather than failing to build. They are outside the
initial feature set anyway.

## Bugs in upstream's stock CYD profile

Running the checker against `BOARD_CYD` as shipped turns up six collisions:

```
GPIO 22  CC1101 GDO0  vs  NRF24 #2 CE
GPIO 25  PN532 SS     vs  NRF24 #3 CSN
GPIO 25  both of those vs the XPT2046 touch clock
GPIO 27  CC1101 CS    vs  NRF24 #2 CSN
GPIO 35  CC1101 GDO2  vs  GPS RX
```

The GPIO 25 pair is the interesting one: anyone wiring a PN532 to a CYD by
upstream's defaults lands the chip select on the touchscreen's clock line.
Worth reporting upstream.

## CC1101 TX/RX orientation

The handoff flagged this as the first thing to fix. It is already correct on
upstream main. `subghz.cpp` calls `setGDO(CC1101_GDO0, CC1101_GDO2)`, which
resolves to `setGDO(22, 35)` — GDO0 on the output pin, GDO2 on the input-only
pin. RCSwitch's `enableTransmit`/`enableReceive` agree. Nothing to change.

GPIO 35 being input-only is a useful accident here: the assignment physically
cannot be made backwards without the transmit path silently failing.

## SPI bus

Done, and it turned out to be five devices rather than four: the XPT2046 touch
controller is on the same peripheral as the radios and the SD card, and on a
CYD it is the only input device. The conflict that matters is the GPIO matrix
rather than the clock — MISO can only be sourced from one pad, and touch reads
GPIO 39 while everything else reads GPIO 19.

`SpiBus` now owns the bus. See [spi-bus.md](spi-bus.md).
