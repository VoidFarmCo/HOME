# Halehound

Fork of [CiferTech's ESP32-DIV](https://github.com/cifertech/ESP32-DIV) for a
custom handheld built on a CYD ESP32-2432S028R.

Fork point: `90f7967c4dc7bcd8c09ebdcf421886737516ddc2` (upstream main,
2026-09-01, eleven commits past `v1.7.2`). Upstream is wired up as the
`upstream` remote.

## Licensing

Upstream is **MIT**, not GPL. Copyright (c) 2023 CiferTech; see `LICENSE`,
which stays as-is. Publishing binaries carries no source-disclosure obligation,
and the fork can be licensed however you like as long as the MIT notice is
retained.

## Layout

```
ESP32-DIV/board_halehound.h   board profile: pin overrides + rationale
ESP32-DIV/BoardConfig.h       board selection (BOARD_HALEHOUND is on)
tools/check_pinmap.py         resolves the pin macros, flags collisions
docs/halehound/hardware.md    pin map, wiring decisions, known upstream bugs
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

Current size: **1825489 bytes, 92% of the app partition**, 34% of RAM. That is
tight. Any real feature work needs either a bigger partition scheme or
trimming upstream modules that Halehound does not use.

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

**Windows MAX_PATH.** The toolchain deliberately lives at `~/.hh-esp32`, not
inside the repo. The esp32 core compiles with `-fno-rtti`, which selects the
`no-rtti` libstdc++ multilib, and with the core inside this repo the path to
`.../xtensa-esp32-elf/no-rtti/bits/error_constants.h` came to 259 characters —
one under the 260 limit. The compiler reported the header as missing while it
sat right there, and only that one multilib was affected, so the default build
worked and `-fno-rtti` did not. Override the location with `HH_ARDUINO_ROOT`
if you must, but keep it short.

**The patched `platform.txt`.** Upstream ships one and the build needs it.
`-DNFC_INTERFACE_SPI` puts the PN532 library into SPI mode. `-zmuldefs` lets
`wifi.cpp` override the IDF's `ieee80211_raw_frame_sanity_check` so raw 802.11
frames can be injected, which is load-bearing and cannot be done with
`--wrap`. It was also swallowing 30 unrelated duplicate symbols, one of them a
real bug; those are fixed during setup. See
[docs/halehound/zmuldefs.md](docs/halehound/zmuldefs.md). `-w` still silences
every compiler warning in the build.

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

- `board_halehound.h` — board profile, resolving all pin conflicts
- `gps.cpp` — `gpsPortOpen()`/`gpsPortClose()` bracket every UART open/close and
  hand GPIO 1 between the console and the GPS
- `tools/check_pinmap.py` — pin map checker
- `tools/build.sh` — pinned, isolated toolchain, build, and CC1101 patches
- `docs/halehound/zmuldefs.md` — what `-zmuldefs` was hiding
- `.github/FUNDING.yml` — fork funding, upstream's Patreon kept

None of this is tested on hardware yet. It compiles, the board profile is
confirmed present in the flash image, and that is all that is currently known.
