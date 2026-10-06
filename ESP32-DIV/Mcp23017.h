#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Mcp23017 — the MCP23017 16-bit I2C GPIO expander, without a driver library.
 *
 * Same reasoning as Nrf24Raw: the register interface is short, and keeping it
 * here (no Adafruit/library dependency, no licence tangle) means one copy. On
 * the owner's board (docs/home/board.md) this carries the radio chip-selects
 * (CC1101 CS, nRF24 CSN/CE, LoRa CS/RESET) because the board breaks out only
 * three direct output pins -- not enough for CC1101 + nRF24 + LoRa together.
 *
 * Pins are numbered 0..15: 0..7 are port A (GPA0..7), 8..15 are port B.
 * These lines are chip-selects and enables -- slow, once-per-transaction -- so
 * I2C latency is fine. Fast lines (the SPI bus, CC1101 GDO0, LoRa BUSY) stay on
 * direct GPIO, never here.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Mcp23017 {

/* Start the expander on the given I2C pins/address and push a known state (all
 * inputs, no pull-ups, latches low). Returns present(). */
bool begin(int sda, int scl, uint8_t addr = 0x20);

/* True when the chip ACKs its address on the bus. */
bool present();

/* mode is the Arduino OUTPUT / INPUT / INPUT_PULLUP. */
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t val);
int  digitalRead(uint8_t pin);

/* Expander-aware helpers. A pin >= PIN_BASE lives on the MCP23017 (pin - PIN_BASE
 * is its 0..15 channel); anything below is a plain ESP32 GPIO. A driver whose
 * control lines may sit on the expander calls these instead of the Arduino
 * globals, so the one driver serves a direct-pin board and an expander board
 * with no change -- the board overlay decides by giving the pin a value above or
 * below PIN_BASE. */
constexpr int PIN_BASE = 100;
inline bool owns(int pin) { return pin >= PIN_BASE; }
void pinModeAny(int pin, uint8_t mode);
void writeAny(int pin, uint8_t val);
int  readAny(int pin);

}  // namespace Mcp23017
