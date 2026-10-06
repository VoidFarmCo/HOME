# H.O.M.E board — the owner's real 3.5" board (not Pueo's)

Verified from a photo, 2026-10-06. **This replaces reliance on `docs/pueo/hardware.md`
and `board_pueo.h`, which describe magikh0e's Sunton 3248S035R — a DIFFERENT board.**

## What it is
A **3.5" "ESP32-32E N4" LCD board, 320x480 resistive touch** with JST breakout headers
(Pueo's board had almost none). Everything plugs into a header — no soldering to the
ESP32 module's castellations.

## Headers (silkscreen)
| Header | Pins |
|---|---|
| **I2C** | `3V3, IO32(SDA), IO25(SCL), GND` |
| **SPI** | `IO23(MOSI), IO19(MISO), IO18(SCK), IO21(CS)` |
| **UART** | `RXD, TXD, GND, 5V`  (TXD = IO1, RXD = IO3) |
| 2-pin | `IO35, IO39`  (both INPUT-ONLY) |
| others | SPEAKER, BAT, microSD slot, RGB LED, RESET, BOOT, micro-USB |

## Fixed by the board (don't touch — these already work)
Display/touch/SD on their OWN bus: TFT SCLK 14, MOSI 13, MISO 12, CS 15, DC 2, RST -1,
backlight 27, TOUCH_CS 33. SD CS 5 on the VSPI bus (18/23/19). Speaker on GPIO 26.

## Free pins for radios
- **Full GPIO (can drive outputs):** IO21, IO25, IO32  — only THREE.
- **Input-only (can only read):** IO35, IO39.
- Shared SPI bus for peripherals: SCK 18, MOSI 23, MISO 19 (on the SPI header; SD shares it).
- UART header for GPS: TXD (IO1) / RXD (IO3).

## The key finding: the MCP23017 is REQUIRED (you have two: VFC-IO-003/004)
CC1101 + nRF24 need FOUR output lines (CC1101 CS + GDO0, nRF CSN + CE) but the board has
only THREE full-GPIO output pins. **So the two radios cannot both run on direct pins** —
the expander is not optional on this board. Also **GDO0 must be a real GPIO** (it drives
data in raw TX), so it cannot sit on an input-only pin (35/39); only GDO2 can.

Rule for the expander: **slow lines go on the MCP23017** (chip-selects, nRF CE, LoRa
RESET — toggled once per transaction), **fast lines stay direct** (the SPI bus, CC1101
GDO0, LoRa BUSY). I2C bus for the expander = IO32 (SDA) + IO25 (SCL).

## Target wiring (with the MCP23017 — the full radio build)
| Line | Where |
|---|---|
| SPI bus (all radios) | SCK **18**, MOSI **23**, MISO **19** (SPI header) |
| MCP23017 | SDA **32**, SCL **25** (I2C header), addr 0x20 |
| CC1101 CS | MCP23017 pin |
| CC1101 GDO0 | **IO21** (direct — raw-TX data, must be fast) |
| CC1101 GDO2 | **IO35** (direct input) |
| nRF24 CSN / CE | MCP23017 pins |
| LoRa (Core1262) CS / RESET | MCP23017 pins |
| LoRa BUSY | **IO39** (direct input — polled fast) |
| LoRa DIO1 | polled over SPI (no pin) |
| GPS | UART header: GPS **TX -> board TXD (IO1)**, GPS VCC = **3.3 V** (not 5 V) |
| SD | CS 5 (onboard) |

## Status
- **Nothing is wired yet** — firmware pinout set first, then solder to match.
- `board_home.h` exists (display-correct) but is NOT the active build yet: the radio pins
  wait on the MCP23017 driver + CS-via-expander plumbing (the libraries drive CS as a
  GPIO; routing it through the expander is the next unit). The UI build runs on the
  identical BOARD_CYD display path meanwhile.
- Lean fallback (no expander): CC1101 + GPS only (CС1101 CS 21, GDO0 32, GDO2 35), nRF/LoRa
  need the expander.
