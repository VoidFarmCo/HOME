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
arduino-cli core install esp32:esp32
```

Libraries are hand-assembled; see upstream's `Libraries/` (TFT_eSPI needs
`User_Setup cyd.h` copied over its `User_Setup.h`, and the CC1101 driver ships
as a zip).

## Scope

Initial targets: SubGHz capture/replay, NFC read/clone, GPS wardriving, jam
detection. Everything else upstream ships stays compiled but untested on this
hardware.

## Changes so far

- `board_halehound.h` — board profile, resolving all pin conflicts
- `gps.cpp` — `gpsPortOpen()`/`gpsPortClose()` bracket every UART open/close and
  hand GPIO 1 between the console and the GPS
- `tools/check_pinmap.py` — pin map checker
- `.github/FUNDING.yml` — fork funding, upstream's Patreon kept
