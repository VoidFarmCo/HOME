# What `-zmuldefs` was hiding

Upstream's patched `platform.txt` adds `-zmuldefs` to the link, which tells the
linker to accept duplicate symbol definitions instead of erroring, keeping
whichever it saw first. Removing it and reading the errors turns up **60 link
errors, 30 unique duplicate symbols**, from three unrelated causes.

Reproduce with:

```bash
sed -i 's/^compiler\.c\.elf\.libs\.esp32=-zmuldefs /compiler.c.elf.libs.esp32=/' \
  "$HH_ARDUINO_ROOT/data/packages/esp32/hardware/esp32/2.0.10/platform.txt"
```

## 1. The one that is intentional

```
wifi.cpp:4295: multiple definition of `ieee80211_raw_frame_sanity_check'
```

`wifi.cpp` defines:

```c
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
    return 0;
}
```

This deliberately overrides the IDF's implementation in `libnet80211.a` so
`esp_wifi_80211_tx()` will accept hand-built frames. Without it, deauth and
beacon features do not work. This is the reason `-zmuldefs` is in the build,
and it has to stay.

`-Wl,--wrap=ieee80211_raw_frame_sanity_check` is **not** a substitute.
`--wrap` only redirects undefined references, and both the function and its
caller live in the same archive member:

```
libnet80211.a:ieee80211_output.o:0000005c T ieee80211_raw_frame_sanity_check
libnet80211.a:ieee80211_output.o:0000002c T esp_wifi_80211_tx
```

The call never becomes an undefined reference, so there is nothing to wrap.

## 2. A dead copy of the whole CC1101 driver (29 symbols)

`SmartRC-CC1101-Driver-Lib` ships two files that are near-identical copies of
each other:

```
ELECHOUSE_CC1101_SRC_DRV.cpp
ELECHOUSE_CC1101_SRC_JT_DRV.cpp
```

Both define `class ELECHOUSE_CC1101`, both define the global object
`ELECHOUSE_cc1101`, and both define the same 28 module-level globals:
`SS_PIN`, `MOSI_PIN`, `MISO_PIN`, `SCK_PIN`, `GDO0`, `GDO2`, the `_M` array
variants, `PA_TABLE*`, `modulation`, `chan`, `trxstate`, `clb1`-`clb4` and so
on.

Two different classes sharing one name in a single program is an ODR
violation, and the linker was picking whichever it saw first. Nothing in the
firmware includes the JT header, so the file is dead weight. `tools/build.sh`
deletes it during setup.

## 3. A real bug: `spi`

This is the one worth caring about.

```
ELECHOUSE_CC1101_SRC_DRV.cpp:45: multiple definition of `spi';
TFT_eSPI/Processors/TFT_eSPI_ESP32.c:13: first defined here
```

The two definitions:

```c
bool spi = 0;                      // CC1101 driver: "use hardware SPI" flag
SPIClass spi = SPIClass(HSPI);     // TFT_eSPI: the display's SPI bus object
```

Both global, both external linkage, same name. The linker folded them onto one
address — confirmed in the linked image, where a single 32-byte symbol covered
both:

```
3ffd0e20 00000020 B spi
```

`SPIClass`'s first member is `int8_t _spi_num` at offset 0, so the CC1101
driver's one-byte flag was sitting exactly on the display's SPI bus number.

What that means in practice:

**Writes.** `setSpiPin()` does `spi = 1`. On the ESP32, `FSPI` is 1 — the bus
attached to the flash chip. So every call wrote `_spi_num = FSPI` into
TFT_eSPI's bus object. The firmware calls `setSpiPin()` six times, once per
SubGHz feature entry.

**Reads.** `setSpi()` guards its platform-default pin table with
`if (spi == 0)`. It was reading `_spi_num`, which is `HSPI` (2) after
construction and never 0, so that fallback could never run.

### Was it actually firing?

Not in the current build, and the reason is luck rather than design.

`_spi_num` is only read in `SPIClass::begin()` and `end()`, and TFT_eSPI calls
`spi.begin()` exactly once, from `TFT_eSPI::init()` at `ESP32-DIV.ino:4517`.
After that every transfer goes through the already-valid `_spi` handle, so a
corrupted bus number goes unnoticed. On the read side, all six `Init()` call
sites are preceded by an explicit `setSpiPin()`, so the dead fallback never
mattered.

It was a landmine, not a fire. Any of these would have set it off:

- re-initialising the display for any reason — theme change, sleep/wake, error
  recovery — which would call `spiStartBus(FSPI, ...)` on the flash bus
- adding a SubGHz path that calls `Init()` without `setSpiPin()` first, which
  would then run with `SCK_PIN`/`MISO_PIN`/`MOSI_PIN`/`SS_PIN` all zero
- enabling `TFT_SDA_READ` in `User_Setup.h`, which calls `spi.begin()` on every
  display read

### Fix

Give the CC1101 flag internal linkage. Nothing outside that translation unit
references it — there is no `extern` for it in any header.

```c
static bool spi = 0;
```

Confirmed in the rebuilt image, two symbols at different addresses where there
had been one:

```
3ffd2189 00000001 b _ZL3spi     CC1101's flag, file-local
3ffd0e20 00000020 B spi         TFT_eSPI's SPIClass, unmolested
```

## Where this leaves things

`-zmuldefs` now covers exactly the one case it was meant for. The other 30
collisions are gone, so a new duplicate symbol introduced from here on is
still silently swallowed — the flag is global. If that matters later, the
narrower option is `-Wl,--allow-multiple-definition` applied only to the link
step that needs it, or moving the `ieee80211` override into its own
translation unit and linking that with its own flags.

`-w` is untouched and still suppresses every compiler warning in the build.
That is the next thing worth pulling on.
