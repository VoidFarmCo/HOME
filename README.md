# Pueo

**A handheld multi-radio field tool.** Open-source firmware for the
ESP32-2432S028R "cheap yellow display" — a 2.8" touchscreen ESP32 carrying a
CC1101 for sub-GHz, an NRF24L01+PA+LNA for 2.4 GHz, a PN532 for NFC and an
ATGM336H for GPS, in a printed enclosure zoned to keep the radios apart.

It covers Wi-Fi and BLE reconnaissance, sub-GHz capture and replay, NFC read
and clone, GPS wardriving, and jam detection.

> [!WARNING]
> **This has never run on hardware.** Everything here compiles, and several
> of the fixes are verified at the symbol level, but no part of it has been
> flashed to a board. Treat every feature claim below as inherited from
> upstream rather than tested. See the status note in [PUEO.md](PUEO.md).

> [!CAUTION]
> For **educational and research use only**. Use only on networks and devices
> you own or have written permission to test. Several of these tools transmit,
> jam, or impersonate. Unauthorised use is illegal in most jurisdictions.

## Lineage

Pueo is a fork of **[ESP32-DIV](https://github.com/cifertech/ESP32-DIV) by
CiferTech**, MIT licensed and staying that way. Nearly every feature here is
theirs; go star the original.

Forked at `90f7967c4dc7bcd8c09ebdcf421886737516ddc2` (upstream main,
2026-09-01, eleven commits past `v1.7.2`). Upstream is kept as a read-only
`upstream` remote for cherry-picking, but Pueo no longer tracks it.

The fork exists because upstream targets its own hardware and the ESP32-S3,
while this targets a stock CYD with modules bolted on. That board has real
pin conflicts and a shared SPI bus, and resolving them meant changes too
invasive to send back as a patch.

## Hardware

A stock CYD (ESP32-WROOM, ILI9341, XPT2046 touch, SD slot) plus:

| Module | Interface | Notes |
|---|---|---|
| CC1101 (HW-863) | VSPI, CS 27 | sub-GHz, board-mounted SMA |
| NRF24L01+PA+LNA | VSPI, CSN 4 / CE 16 | 2.4 GHz, needs its own 3.3 V rail |
| PN532 V3 | VSPI, SS 17 | NFC, SPI mode |
| ATGM336H | UART, RX on GPIO 1 | GPS, antenna on a u.FL pigtail |

Three of those pins are the CYD's onboard RGB LED, which Pueo gives up. The
GPS lands on GPIO 1 — UART0's *transmit* pin — which looks wrong and is
deliberate; [docs/pueo/hardware.md](docs/pueo/hardware.md) explains why, and
`gpsPortOpen()`/`gpsPortClose()` hand the pin between the console and the GPS.

Run `python tools/check_pinmap.py` to print the map and check for collisions.

## Features

Inherited from ESP32-DIV unless marked.

| Wi-Fi | 2.4 GHz | Sub-GHz | BLE | Other |
|---|---|---|---|---|
| Packet Monitor | Scanner | Replay Attack | BLE Scanner | RFID/NFC read |
| Wi-Fi Scanner | Proto Kill | SubGHz Jammer | BLE Jammer | Card clone / erase |
| Beacon Spammer | ESB Sniffer | De Bruijn / Brute | BLE Spoofer | Tag disrupt |
| Deauther | ESB Replay | Jamming Detector | Sour Apple | GPS wardriver |
| Deauth Detector | MouseJack Scan | Saved Profiles | AirTag Spoofer | Satellite scanner |
| Probe Req Flood | MouseJack Inject | | AirTag Sniffer | SD file manager |
| Captive Portal | | | Skimmer Detect | Serial monitor |
| Hidden SSID | | | **Spotter** *(new)* | BLE Ducky |
| WPS / ARP scan | | | | |
| Karma Attack | | | | |

**Spotter** is the one feature Pueo adds: passive detection of ALPR cameras
and smart glasses, matching Wi-Fi OUIs and BLE service UUIDs against a
signature table. It only listens — it never transmits.

**IR is gone.** There is no IR LED or receiver on this board, so its
protocol tables could never run here. Removing them took flash from 93% to
85%.

## Building

```sh
tools/build.sh setup      # once, installs a pinned toolchain (~1 GB)
tools/build.sh            # compile
tools/build.sh merge      # single flash image, write to offset 0
tools/build.sh upload COM7
```

Needs `arduino-cli` and Python 3. Everything installs into its own root
rather than a global Arduino install, so it cannot disturb another project.

**Read "Things that will bite you" in [PUEO.md](PUEO.md) before deviating
from the script.** The esp32 core version, the pinned library versions and
the toolchain path length are all load-bearing — on Windows the last one is
a `MAX_PATH` trap that reports a header as missing when it is right there.

The build compiles clean under `-Wall -Wextra`. Upstream's `platform.txt`
carries `-w` and `-zmuldefs`; `build.sh` strips both, and
[docs/pueo/warnings.md](docs/pueo/warnings.md) and
[zmuldefs.md](docs/pueo/zmuldefs.md) record what each was hiding — including
one linker override that turned out to be load-bearing.

## Documentation

| | |
|---|---|
| [PUEO.md](PUEO.md) | build, layout, scope, and what will bite you |
| [docs/pueo/hardware.md](docs/pueo/hardware.md) | pin map and the GPIO 1 handover |
| [docs/pueo/spi-bus.md](docs/pueo/spi-bus.md) | the shared bus, and why touch was losing it |
| [docs/pueo/warnings.md](docs/pueo/warnings.md) | what `-w` was hiding |
| [docs/pueo/zmuldefs.md](docs/pueo/zmuldefs.md) | what `-zmuldefs` was hiding |
| [docs/pueo/pcb-design.md](docs/pueo/pcb-design.md) | carrier board: netlist, power tree, placement |
| [docs/pueo/pueo-enclosure.scad](docs/pueo/pueo-enclosure.scad) | the printed enclosure |

## What changed from upstream

Mostly the parts that decide whether the hardware works at all:

- **`board_pueo.h`** — board profile resolving every pin conflict in the
  stock CYD map
- **`SpiBus.{h,cpp}`** — a single owner for the VSPI bus the display, SD card
  and all three radios share
- **`wifi.cpp`** — removed an out-of-bounds write present in both deauth
  frame builders
- **CC1101 driver** — vendored; `SpiEnd()` no longer calls `SPI.end()` after
  every register access, which was tearing the peripheral out from under
  touch and the SD card
- **Warnings** — `-w` and `-zmuldefs` removed from the build, and the
  underlying issues fixed rather than silenced

## License

MIT. Copyright (c) 2023 CiferTech — see [LICENSE](LICENSE), which is
unchanged. The MIT notice stays with any redistribution.

## Credits

**[CiferTech](https://github.com/cifertech)** wrote ESP32-DIV, which is
almost all of this. Support the original project:
[patreon.com/cifertech](https://www.patreon.com/cifertech).

*Pueo* is the Hawaiian owl — a low, silent flier that hunts by listening.
