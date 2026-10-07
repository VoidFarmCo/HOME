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

## Target wiring — the full radio build (matches board_home.h)

### Step 1: the MCP23017 expander (do this first)
It sits on the **I2C header** and carries the slow chip-select / control lines.
| MCP23017 pin | Wire to |
|---|---|
| VDD | 3V3 (I2C header) |
| GND | GND (I2C header) |
| SDA | **IO32** (I2C header SDA) |
| SCL | **IO25** (I2C header SCL) |
| A0 / A1 / A2 | all GND -> I2C address **0x20** |
| RESET | 3V3 (tie high, never reset) |

### Step 2: the shared SPI bus (all three radios share these)
From the **SPI header**: **SCK = IO18, MOSI = IO23, MISO = IO19**. Wire each radio's
SCK/MOSI/MISO to these same three pins (plus 3V3 + GND to each).

### Step 3: each radio's control lines
| Radio | Line | Wire to |
|---|---|---|
| **CC1101** (433) | CS   | MCP23017 **GPA0** (channel 0) |
| | GDO0 | **IO21** (direct — raw-TX data, must be fast; SPI header "CS" pin) |
| | GDO2 | **IO35** (direct, input-only header pin) |
| **nRF24** (2.4) | CSN | MCP23017 **GPA1** (channel 1) |
| | CE  | MCP23017 **GPA2** (channel 2) |
| | IRQ | not used (leave unconnected) |
| **LoRa** (Waveshare SX1262 Node HF) | CS / NSS | MCP23017 **GPA3** (channel 3) |
| | RESET | MCP23017 **GPA4** (channel 4) |
| | BUSY  | **IO39** (direct, input-only header pin — polled fast) |
| | TXEN  | MCP23017 **GPA5** (channel 5) — firmware drives HIGH in RX/idle, LOW during TX |
| | RXEN / DIO2 | **leave on the module, do NOT wire to the ESP** (DIO2 drives RXEN automatically) |
| | DIO1  | not wired (polled over SPI) |
| | 3V3 / GND | power; ANT via u.FL to the spring antenna |

**Which LoRa module:** use the **Waveshare "SX1262 LoRa Node (HF)"** (SX1262, SPI, 22 dBm — the
one with RXEN/TXEN/BUSY). The others are NOT compatible with this firmware: **XL1276/XL1278** is
a different chip (SX1276/78) and **REYAX RYLR998** is a UART AT module (not SPI). Set them aside.

RF-switch note: the firmware follows Waveshare's stated TXEN polarity (HIGH=RX, LOW=TX) because
it is the safe direction — the wrong guess could put 22 dBm into the receive LNA. Bench-test LoRa
at low power first; if it receives but never transmits, TXEN polarity is one line to flip.

(MCP23017 GPA0..GPA5 = the firmware's pin values 100..105 = `PIN_BASE + channel`.)

### Step 4: GPS + SD (no expander)
| Line | Where |
|---|---|
| GPS | UART header: GPS **TX -> board TXD (IO1)**; GPS **VCC = 3.3 V** (NOT 5 V); GND |
| SD | onboard, CS 5 (already wired) |

Power note: give the radios a solid 3V3 supply — CC1101 + nRF24(PA/LNA) + LoRa all
drawing at once off a weak rail is the kind of thing that causes brownout resets.

## Status
- **Firmware side is DONE and active** — `board_home.h` is the live build (BOARD_HOME).
  The MCP23017 driver, nRF/CC1101/LoRa CS-via-expander plumbing, and the SX1262 LoRa
  driver are all in. The firmware probes each radio at runtime and refuses features whose
  radio is absent, so a half-wired board is safe to boot.
- **Nothing is wired yet on the hardware** — solder to the tables above, then the radios
  come alive feature by feature as you connect them (boot log prints "MCP23017 not found"
  until the expander is wired).
- Can't be tested here until wired: LoRa mesh end-to-end, SubGHz chat TX, nRF presence.
- Lean fallback (no expander): CC1101 + GPS only (CС1101 CS 21, GDO0 32, GDO2 35), nRF/LoRa
  need the expander.
