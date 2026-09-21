# Pueo hardware

CYD ESP32-2432S028R with four external peripherals, in a custom 85 x 170 x 20
enclosure. The pin map below is fixed; the case is already printed around it.

For the order to wire it in, and which six of the ten signals need soldering
rather than a header, see [build-guide.md](build-guide.md).

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
| GPS TX -> ESP32 | 1 | UART0 TX pin on the P5 JST, see below |
| GPS RX | not connected | |

**Which 2.8" board you have matters more than it should.** "Cheap yellow
display" names boards from at least two vendors. This tree targets Sunton's
**ESP32-2432S028R**, whose silkscreen reads `ESP32 2432S028`. lcdwiki's
2.8" is the **E32R28T** -- a different board, 50.00 x 86.00 mm against the
Sunton's larger outline, and it puts the RGB LED's red channel on GPIO 22
and an audio amplifier's enable on GPIO 4. Nothing here has been built for
it, and the 2.8" image would drive that amplifier as a chip select.

**The two boards do not agree about GPIO 4.** On the 2.8" ESP32-2432S028R it
is the RGB LED's red channel. On the 3.5" ESP32-3248S035R -- lcdwiki's E32R35T,
55.50 x 101.50 x 5.80 mm -- it is the **audio amplifier's enable**, and the RGB
LED's red channel moves to GPIO 22.

Spending an LED is the trade this pin map already makes. Keying an amplifier
enable at chip-select rates is not: it clicks, and it draws current off a rail
already carrying the display. So NRF24 CSN follows the panel, 4 on the 2.8" and
**25 on the 3.5"**. 25 is free there for the same reason it is not free on the
2.8": that panel puts touch on its own bus at 25/32/39, while the 3.5" hangs its
XPT2046 off the display's SPI behind TOUCH_CS. The pin that collides on one
board is the spare on the other.

The 2.8" is left alone deliberately. A published pin map is a thing people have
already soldered to.

Two more differences on the 3.5", neither of which Pueo drives: GPIO 34 is the
battery divider rather than an LDR, and GPIO 36 is the touch IRQ. GPIO 26 is the
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
