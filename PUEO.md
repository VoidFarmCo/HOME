# Pueo

Open-source firmware for a handheld multi-radio field tool built on the
ESP32-2432S028R "cheap yellow display" — a 2.8" touchscreen ESP32 carrying a
CC1101 for sub-GHz, an NRF24L01+PA+LNA for 2.4 GHz, a PN532 for NFC and a
GT-U7 for GPS, in a printed enclosure zoned to keep the radios apart. It
covers WiFi and BLE reconnaissance, sub-GHz capture and replay, NFC read and
clone, GPS wardriving and jam detection. It is a fork of CiferTech's
ESP32-DIV, MIT licensed and staying that way, diverging mainly in the parts
that decide whether the hardware works at all: a board profile that resolves
the pin conflicts in the stock CYD map, a single owner for the SPI bus that
the display, SD card and all three radios share, and a build that compiles
clean under `-Wall -Wextra` rather than suppressing its own warnings.

> **Status: not yet run on hardware.** Everything here compiles and is
> reasoned from the source, and several of the fixes are verified at the
> symbol level, but no part of it has been flashed to a board. Treat feature
> claims as inherited from upstream rather than tested.

Lineage: forked from [CiferTech's ESP32-DIV](https://github.com/cifertech/ESP32-DIV)
at `90f7967c4dc7bcd8c09ebdcf421886737516ddc2` (upstream main, 2026-09-01,
eleven commits past `v1.7.2`). Upstream is kept as a read-only `upstream`
remote for cherry-picking, but Pueo no longer tracks it.

## Licensing

Upstream is **MIT**, not GPL. Copyright (c) 2023 CiferTech; see `LICENSE`,
which stays as-is. Publishing binaries carries no source-disclosure obligation,
and the fork can be licensed however you like as long as the MIT notice is
retained.

## Layout

```
ESP32-DIV/board_pueo.h   board profile: pin overrides + rationale
ESP32-DIV/BoardConfig.h       board selection (BOARD_PUEO is on)
tools/check_pinmap.py         resolves the pin macros, flags collisions
docs/pueo/hardware.md    pin map, wiring decisions, known upstream bugs
```

The board profile is an *overlay*, not a fourth board branch. Every pin macro
in `shared.h` is wrapped in `#ifndef`, and `BoardConfig.h` is included before
those defaults are evaluated, so the overlay wins by defining pins first. It
also defines `BOARD_CYD`, so display, touch, SD and UI behaviour follow the
stock CYD path. Net effect: `git merge upstream/main` touches one line of
`BoardConfig.h` at worst, instead of conflicting across every pin block.

## Checking the pin map

```bash
python tools/check_pinmap.py
```

Runs a cut-down preprocessor over `shared.h` and reports the resolved GPIO for
every signal that drives a pad, then checks for three things: two signals on
one pin, a signal landing on CYD hardware that isn't consciously repurposed,
and an output assigned to an input-only pad (GPIO 34-39). Exit 1 on any hit.

It is not a substitute for compiling. It is a substitute for reading six nested
`#ifdef` blocks and hoping.

## Building

Upstream is an Arduino sketch, so `arduino-cli` is the path of least
resistance. PlatformIO would mean restructuring the tree first.

```bash
tools/build.sh setup
```

```bash
tools/build.sh
```

```bash
tools/build.sh upload COM7
```

`setup` is a one-time ~1 GB download. It installs everything into its own root
rather than a global Arduino install, so it cannot disturb other projects.

Current size: **1690265 bytes, 85% of the app partition**, 33% of RAM.

It was at 93% before the IR module came out. Pueo has no IR LED and no IR
receiver, so `ir.cpp` and the IRremoteESP8266 protocol tables it pulled in
were 140 KB of code that could never run on this board. Trimming upstream
modules this hardware cannot reach is the cheapest headroom available, and
there is more of it: Ducky/BadUSB is the next-largest piece with no
corresponding hardware.

### Emulation

`tools/build.sh merge` produces a single flash image at offset 0. Padded to
4 MB it boots under Espressif's QEMU:

```bash
qemu-system-xtensa -nographic -machine esp32 -drive file=pueo-4mb.bin,if=mtd,format=raw
```

It gets as far as IDF core init and then stops:

```
assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)
```

That is `esp_flash_init_default_chip()` rejecting QEMU's emulated flash. The
QEMU shipped with the current IDF installer is built against the IDF 5/6 line
while this firmware is Arduino core 2.0.10, which is IDF 4.4 -- the flash chip
detection does not line up. Nothing to do with the firmware: the ROM loader,
the second-stage bootloader and the app image all load correctly first.

Worth being clear about the ceiling even if that were fixed. QEMU models the
CPU, RAM, UART and timers. It does not model the ILI9341, the XPT2046, the
CC1101, the NRF24, the PN532 or the WiFi radio -- which is to say, all of the
things actually worth testing here. A perfect boot under QEMU would prove
`setup()` reaches the point where it touches hardware, and nothing beyond it.

### Things that will bite you

**esp32 core 2.0.10, exactly.** Upstream documents this and means it. The 3.x
line is IDF 5, which dropped `esp_event_loop.h` — `config.h` includes it, so
3.x fails on the first file.

**Library versions are pinned, and not for neatness.** Library Manager hands
you the newest, and three of these broke their APIs: ArduinoJson 7 dropped
`StaticJsonDocument` and `createNestedObject`, NimBLE 2.x dropped
`NimBLEAdvertisedDeviceCallbacks` and `NimBLESecurity` and renamed the
`NimBLEHIDDevice` accessors, and arduinoFFT 2.x replaced the `arduinoFFT`
class with `ArduinoFFT<T>`. Between them that is roughly twenty compile
errors that look like code bugs and are not.

**TFT_eSPI and the CC1101 driver must come from `Libraries/`.** Upstream
customised both. `User_Setup cyd.h` has to land as TFT_eSPI's `User_Setup.h`.

**Windows MAX_PATH.** The toolchain deliberately lives at `~/.pueo-esp32`, not
inside the repo. The esp32 core compiles with `-fno-rtti`, which selects the
`no-rtti` libstdc++ multilib, and with the core inside this repo the path to
`.../xtensa-esp32-elf/no-rtti/bits/error_constants.h` came to 259 characters —
one under the 260 limit. The compiler reported the header as missing while it
sat right there, and only that one multilib was affected, so the default build
worked and `-fno-rtti` did not. Override the location with `PUEO_ARDUINO_ROOT`
if you must, but keep it short.

**The patched `platform.txt`.** Upstream ships one and the build needs it.
`-DNFC_INTERFACE_SPI` puts the PN532 library into SPI mode. `-zmuldefs` lets
`wifi.cpp` override the IDF's `ieee80211_raw_frame_sanity_check` so raw 802.11
frames can be injected, which is load-bearing and cannot be done with
`--wrap`. It was also swallowing 30 unrelated duplicate symbols, one of them a
real bug; those are fixed during setup. See
[docs/pueo/zmuldefs.md](docs/pueo/zmuldefs.md). `-w` is **gone** --
`setup` strips it and the build runs `-Wall -Wextra`, which the sketch is
clean under. See [docs/pueo/warnings.md](docs/pueo/warnings.md).

**The vendored CC1101 driver is patched during setup.** It shipped a dead
copy-paste clone of itself (`..._JT_DRV.cpp`, a second `class
ELECHOUSE_CC1101` and 29 duplicate globals) and declared its hardware-SPI flag
as a global named `spi`, which collided with TFT_eSPI's `SPIClass spi`. See
the same document.

## Scope

Initial targets: SubGHz capture/replay, NFC read/clone, GPS wardriving, jam
detection. Everything else upstream ships stays compiled but untested on this
hardware.

## Changes so far

- `board_pueo.h` — board profile, resolving all pin conflicts
- `gps.cpp` — `gpsPortOpen()`/`gpsPortClose()` bracket every UART open/close and
  hand GPIO 1 between the console and the GPS
- `tools/check_pinmap.py` — pin map checker
- `tools/build.sh` — pinned, isolated toolchain, build, and CC1101 patches
- `wifi.cpp` — removed an out-of-bounds write in both deauth frame builders
- `SpiBus.{h,cpp}` — single owner for the shared VSPI bus, and the fix for
  touch losing the bus to the radios
- `docs/pueo/zmuldefs.md` — what `-zmuldefs` was hiding
- `docs/pueo/warnings.md` — what `-w` was hiding
- `wifi.cpp`, `bluetooth.cpp`, `subghz.cpp`, `utils.cpp` — the per-screen UI
  macros are scoped constants now, so `-w` could come off
- `docs/pueo/spi-bus.md` — the bus map, and why touch was losing it
- `.github/FUNDING.yml` — fork funding, upstream's Patreon kept

None of this is tested on hardware yet. It compiles, the board profile is
confirmed present in the flash image, and that is all that is currently known.
