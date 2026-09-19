# Pueo hardware

CYD ESP32-2432S028R with four external peripherals, in a custom 85 x 170 x 20
enclosure. The pin map below is fixed; the case is already printed around it.

## Pin map

Machine-checked by `tools/check_pinmap.py`, which resolves the macros the way
the compiler will and cross-references them against the CYD's own wiring.

| Signal | GPIO | Notes |
|---|---|---|
| VSPI SCK | 18 | shared bus |
| VSPI MOSI | 23 | shared bus |
| VSPI MISO | 19 | shared bus |
| SD CS | 5 | onboard slot |
| CC1101 CS | 27 | CN1 header |
| CC1101 GDO0 (TX) | 22 | P3 header |
| CC1101 GDO2 (RX) | 35 | P3 header, input-only pin |
| NRF24 CSN | 4 | was RGB LED red |
| NRF24 CE | 16 | was RGB LED green |
| NRF24 IRQ | not connected | see below |
| PN532 SS | 17 | was RGB LED blue, SPI mode (DIP CH1=OFF, CH2=ON) |
| GPS TX -> ESP32 | 1 | UART0 TX pin on the P1 JST, see below |
| GPS RX | not connected | |

The onboard RGB LED is gone. GPIO 4/16/17 are the only contiguous spare pins on
this board, and three radios need six lines.

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
RF24 driver is polled throughout. Leave the pin off the board.

## One NRF24, three radio objects

`bluetooth.cpp` instantiates `radio1`/`radio2`/`radio3` for the multi-channel
BLE jammer modes. Pueo has one module, so `CE_PIN_2`/`_3` and
`CSN_PIN_2`/`_3` are aliased onto the same pads as `_1`. Those modes will run
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
