#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Sx1262 — the SX1262 (Core1262) LoRa radio, command interface, no library.
 *
 * Same house approach as Nrf24Raw / Mcp23017: the part speaks a command protocol
 * (opcode + args over SPI, gated by a BUSY line), and the slice we need -- bring
 * up, send one packet, receive one packet -- is small enough to own here without
 * a GPL/licence dependency. This is the radio under the private H.O.M.E LoRa mesh
 * (the mesh/routing lives above this, in the chat transport).
 *
 * On the owner's board (docs/home/board.md) CS and RESET are on the MCP23017 and
 * BUSY is a direct GPIO; all pin access goes through Mcp23017::writeAny/readAny so
 * the same code serves the expander board and a direct-pin board. BUSY MUST be
 * polled low before every command -- that is the whole reason it stays direct.
 *
 * Scope is deliberately small and synchronous: no interrupts (DIO1 is polled via
 * GetIrqStatus), fixed LoRa modulation suitable for text. A feature that needs
 * more belongs in this file, not a second copy.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <stddef.h>

namespace Sx1262 {

/* Bring the radio up at the given frequency (Hz, e.g. 915000000). Returns
 * present(): false if the chip never answers (e.g. module not wired). */
bool begin(uint32_t freqHz);

/* True when the chip responds on the bus (status read after reset). */
bool present();

/* Transmit len bytes (<= 255). Blocks until TxDone or a timeout. Returns false
 * on timeout or if not present. */
bool send(const uint8_t* data, uint8_t len);

/* Put the radio in continuous receive. Call once before polling receive(). */
void startReceive();

/* If a packet has arrived, copy up to maxLen bytes into buf and return its
 * length (RSSI in rssiOut if non-null); 0 if nothing is waiting. Re-arms RX. */
uint8_t receive(uint8_t* buf, uint8_t maxLen, int16_t* rssiOut = nullptr);

}  // namespace Sx1262
