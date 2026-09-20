# SPI bus arbitration

The handoff put this first: *"Four devices on VSPI with different clock
requirements. Get the abstraction right before anything else — retrofitting it
is painful."*

It is five devices, not four, and the thing that actually conflicts is not the
clock.

## The bus map

The ESP32 has two general-purpose SPI peripherals. TFT_eSPI takes HSPI for the
display. Everything else shares VSPI:

| device | CS | SCK | MISO | MOSI | driver behaviour |
|---|---|---|---|---|---|
| XPT2046 touch | 33 | 25 | **39** | 32 | own `beginTransaction`, 2 MHz |
| SD card | 5 | 18 | **19** | 23 | own frequency via `SD.begin` |
| CC1101 | 27 | 18 | **19** | 23 | **bare `SPI.transfer()`, no transaction** |
| NRF24 | 4 | 18 | **19** | 23 | own `beginTransaction`, 16 MHz |
| PN532 | 17 | 18 | 19 | 23 | **bit-bangs the pads**, LSB-first |

Touch is the one that was missing from the handoff's list, and it is the one
that matters most, because it is the only input device on a CYD.

## The real conflict is the GPIO matrix, not the clock

SCK and MOSI are peripheral *outputs*. The ESP32's matrix can fan one output
signal to several pads, so several devices' clock lines can stay wired up at
once harmlessly.

MISO is an *input*. `spiAttachMISO()` ends in:

```c
pinMatrixInAttach(miso, SPI_MISO_IDX(spi->num), false);
```

One GPIO drives the peripheral's MISO signal. Attaching a second one replaces
the first. Touch reads on **GPIO 39** and everything else reads on **GPIO 19**,
so whichever attached last owns the bus and the loser silently reads the wrong
pin. Nothing about that appears in a build log.

## Three things in the Arduino SPI layer that make this worse

All verified by reading the 2.0.10 core, not inferred.

**Both SPIClass objects are the same peripheral.** `spiStartBus()` ends with
`return &_spi_bus_array[spi_num];` — a static, per-bus-number, with no
refcount. On CYD `touchscreenSPI` is `SPIClass(VSPI)` and the global `SPI` is
`SPIClass(VSPI)`, so they hand out the *same* `spi_t*`. `SPI.end()` therefore
calls `spiStopBus()` on the peripheral touch is using, and touch's own
`_spi` pointer is left dangling at a stopped bus.

**`begin()` is a no-op once the bus is running.** `SPIClass::begin()` opens
with `if(_spi) { return; }`. So every

```c
SPI.begin(NRF24_SPI_SCK, NRF24_SPI_MISO, NRF24_SPI_MOSI, NRF24_SPI_SS);
```

on feature entry does nothing whenever the bus is already started. The pin
routing it appears to establish may never happen. This also defeats
`ensureTouchSpiReady()`, which calls `touchscreenSPI.begin(...)` before every
touch sample and looks exactly like a guard against this problem — it cannot
re-attach anything after the first call.

**Clock settings outlive `end()`.** `SPIClass::end()` clears `_spi` but not
`_freq` or `_div`, and `begin()` restarts the bus with the stored `_div`. So
the clock a device runs at is whatever the last device set.

## What that adds up to

**Prediction, not yet observed: on a CYD, touch stops responding the first
time any SD or radio feature runs, and stays dead until reboot.**

Boot order is `tft.init()` → `initSDCard()` (CYD path only raises CS, defers
the mount) → `setupTouchscreen()`, so touch attaches VSPI to GPIO 39 and works.
The first `SPI.begin(...)` or `reclaimSharedSpiBus()` re-points MISO at GPIO 19
and touch is reading a pin nothing answers on. `ensureTouchSpiReady()` cannot
recover it.

Touch is the only input on a CYD — no PCF8574 buttons — so this would make the
device unusable until power-cycled. That is severe enough that it is probably
untested territory rather than a regression, which fits: the stock `BOARD_CYD`
pin map also lands the PN532 chip select on the touch clock, and upstream's own
comments in this area read like a list of past accidents ("Mounting SD here is
what broke SubGHz", "leaves the SD card dead until something else re-inits the
bus").

**Observed in the source: the CC1101 runs at whatever clock the last feature
left.** The ELECHOUSE driver issues bare `SPI.transfer()` with no transaction,
so it never sets its own speed. Most SubGHz entry points call
`reclaimSharedSpiBus()` first, which ends at 4 MHz. Two do not —
`saveSetup()` (`subghz.cpp:2210`) and `subjammerSetup()` (`subghz.cpp:2738`)
reach `setSpiPin()`/`Init()` with no reclaim on the path. Arriving there from
an NRF feature leaves the bus at 10 MHz, above the CC1101's 6.5 MHz
burst-access ceiling.

## The layer

`SpiBus` (`ESP32-DIV/SpiBus.{h,cpp}`) makes ownership explicit.

```c
SpiBus::claim(SpiBus::Dev::Cc1101);
...
SpiBus::release(SpiBus::Dev::Cc1101);
```

`claim()` parks every chip select on the bus, re-points SCK/MISO/MOSI at the
incoming device's pins if the previous owner used different ones, and applies
that device's mode, bit order and clock explicitly rather than inheriting them.
It is a cheap no-op when the device already holds the bus, which matters
because touch claims on every sample.

Re-pointing uses `spiAttachSCK`/`spiAttachMISO`/`spiAttachMOSI` and their
detach counterparts directly, *not* `end()`/`begin()`. Going through SPIClass
would reset the peripheral out from under the other users, for the reasons
above.

The PN532 is the exception: its driver bit-bangs, so `claim(Dev::Pn532)`
detaches the peripheral from those pads and `gpio_reset_pin`s them instead of
routing anything.

Per-device clocks are only meaningful where the driver does not manage its own.
Touch, NRF24 and SD all wrap their transfers in `beginTransaction` or pass a
frequency to `begin`, so for them the profile clock is just an idle setting.
The CC1101 is the one we genuinely own, and it is pinned at 4 MHz.

`SpiBus::stats()` counts claims, re-pins, conflicts (a claim while someone else
still held the bus) and stray releases. `logStats()` dumps them to serial.
Those counters are the cheapest way to check the discipline holds once there is
hardware to run it on.

## What changed at the call sites

The four legacy helpers — `sdReleaseOtherChipSelects`, `reclaimSharedSpiBus`,
`restoreSdAfterSharedSpi`, `holdSdInactiveOnSharedSpi` — were re-expressed on
top of `SpiBus` and kept their names and signatures, so all 29 existing call
sites are untouched. That keeps the diff reviewable and the upstream merge
cheap.

Beyond that:

- `ensureTouchSpiReady()` now claims the bus for touch, which is the fix for
  the prediction above
- six inline `SPI.begin(...)` + three-setter blocks in `bluetooth.cpp` became
  `claim(Dev::Nrf24)` / `claim(Dev::Sd)`
- the two unarbitrated SubGHz entries got `claim(Dev::Cc1101)`
- `rfidAttachBus()` / `rfidRestoreBus()` claim and release `Dev::Pn532`

Cost: **+832 bytes flash, +24 bytes RAM.**

## What this does not do

It is not verified on hardware. Every claim above about the Arduino core is
read from its source and can be checked by anyone; the claim about touch
actually dying is a prediction derived from those, and it is the first thing
to test when a CYD exists.

## The CC1101 driver was the other half of it

Vendored into `libs/` and fixed at source now that the fork is standalone.
Two things it was doing:

```c
void ELECHOUSE_CC1101::SpiEnd(void) {
  SPI.endTransaction();
  SPI.end();          // after every single register access
}
```

`SPI.end()` calls `spiStopBus()`, which resets the peripheral. The touch
controller and the SD card are on that same peripheral and hold the same
`spi_t*`, so **every CC1101 register read tore the bus down for both of
them**. That is the cause behind upstream's symptom comments -- "leaves the
SD card dead until something else re-inits the bus", "Mounting SD here is
what broke SubGHz". It now ends the transaction and nothing more.

And `SpiStart()` called `SPI.begin(SCK, MISO, MOSI, SS)`, which is a no-op
once the bus is running, so the pins it looked like it was claiming were
never claimed. It now opens a transaction instead, which holds the SPI mutex
so a WiFi or BLE task cannot interleave a transfer midway through ours, and
applies the CC1101's 4 MHz from `kProfiles` (under the 6.5 MHz burst ceiling)
rather than inheriting the last device's clock.

See `libs/SmartRC-CC1101-Driver-Lib/VENDORED.md`.

It does not touch `sdTryBeginOrder()`, which still configures the bus inline.
Its comments describe carefully tuned mount-retry ordering ("Do not
`SPI.end()`/gpio_reset here — that tears down CC1101 after SubGHz Init") and
rearranging it blind, with no card to test against, is not worth the risk.

`sdSpiInit()` in `utils.cpp` is dead — nothing calls it. Left in place to keep
the merge surface small; it is misleading if you are reading that file.
