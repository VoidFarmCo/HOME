# SmartRC-CC1101-Driver-Lib, vendored

Upstream: <https://github.com/LSatan/SmartRC-CC1101-Driver-Lib> by LSatan,
version 2.5.7, MIT licensed. This copy came from the zip in ESP32-DIV's
`Libraries/` directory, which ships no `LICENSE` file — drop the upstream one
in here if you redistribute.

It lives in the repo rather than being unpacked and patched at setup time
because Pueo no longer tracks upstream, and because the changes below are not
the kind of thing that should be re-applied by `sed` on every clean install.

## Changes

**`ELECHOUSE_CC1101_SRC_JT_DRV.{cpp,h}` deleted.** A copy-paste clone of the
whole driver: a second `class ELECHOUSE_CC1101`, a second `ELECHOUSE_cc1101`
object and 29 duplicate globals. Nothing included its header. Two different
classes sharing one name in a program is an ODR violation, and it was only
tolerated because upstream linked with `-zmuldefs`.

**`bool spi` given internal linkage.** The driver declared its hardware-SPI
flag as a global named `spi`. TFT_eSPI declares its bus object the same way:

```c
bool spi = 0;                   // here
SPIClass spi = SPIClass(HSPI);  // TFT_eSPI_ESP32.c
```

The linker folded them onto one address. `SPIClass`'s first member is
`int8_t _spi_num`, so this one-byte flag sat on the display's bus number, and
`setSpiPin()` doing `spi = 1` wrote FSPI into it. `static` separates them.

**`SpiEnd()` no longer calls `SPI.end()`.** This is the significant one. The
original ended *every register access* with a full bus teardown:

```c
void ELECHOUSE_CC1101::SpiEnd(void) {
  SPI.endTransaction();
  SPI.end();          // <- resets the entire peripheral
}
```

`SPI.end()` calls `spiStopBus()`. On this board the XPT2046 and the SD card
are on that same peripheral and were handed the same `spi_t*` by
`spiStartBus()`, so every CC1101 register read reset the bus out from under
both of them. ESP32-DIV's own comments describe the symptoms without naming
the cause — "leaves the SD card dead until something else re-inits the bus",
"Mounting SD here is what broke SubGHz". Now it just ends the transaction.

**`SpiStart()` no longer calls `SPI.begin()`, and opens a transaction.**
`SpiBus` owns the pin routing and has already pointed the matrix at the
CC1101 before the driver is called. The `SPI.begin()` was a no-op in any case
— `SPIClass::begin()` returns early once `_spi` is set, so the pins it looked
like it was claiming were never claimed. In its place `beginTransaction()`
holds the SPI mutex for the duration of one access, so a WiFi or BLE task
cannot interleave a transfer halfway through ours.

**`CC1101_SPI_HZ`, default 4 MHz.** The driver never set its own clock, so it
ran at whatever the last device to touch the bus had left. The datasheet
allows 10 MHz single-access but only 6.5 MHz for burst; 4 MHz clears both.

## H.O.M.E: chip-select via MCP23017 (2026-10-06)

All `digitalWrite(SS_PIN, ...)` and `pinMode(SS_PIN, OUTPUT)` are routed through
`ccCsWrite()` / `ccCsMode()`, which call the extern wrappers `homeExpWrite` /
`homeExpMode` (defined in `ESP32-DIV/Mcp23017.cpp`). This lets SS_PIN be an
MCP23017 expander channel on the owner's board (pin >= Mcp23017::PIN_BASE) or a
plain GPIO, with no change here. The lib is compiled apart from the sketch, so it
cannot include the sketch header -- hence the link-time wrappers. Held by
`tools/check_cc1101_expander.py`. build.sh now re-syncs this lib on every compile.
