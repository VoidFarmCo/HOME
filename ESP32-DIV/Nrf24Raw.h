#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Nrf24Raw — the nRF24L01+ register interface, without a driver library.
 *
 * This exists for two reasons.
 *
 * The licensing one: RF24 is GPL-2.0-*only*, which cannot lawfully share a
 * binary with arduinoFFT (GPL-3.0-or-later) or NimBLE-Arduino (Apache-2.0).
 * See docs/pueo/licensing.md. RF24 was the only GPLv2-only thing in the tree
 * and the jammers were the only code still using it; everything else that
 * talks to this chip — MouseJack, the ESB paths, the skimmer detector — was
 * already driving the registers directly.
 *
 * The other one is that those direct paths had grown twice over, once in
 * Scanner and once in EsbSniffer, with a third about to appear here. The
 * register interface to this part is twenty lines. It should exist once.
 *
 * Scope is deliberately small: power, channel, and an unmodulated carrier.
 * Nothing here sends a packet. If a future feature needs the FIFOs, it
 * belongs in this file rather than in a fourth copy somewhere else.
 *
 * One module. Pueo's board profile maps CE_PIN_2/3 and CSN_PIN_2/3 onto
 * CE_PIN_1/CSN_PIN_1 — see board_pueo.h — so the three-radio arrangement
 * upstream assumes is three names for the same chip here.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Nrf24Raw {

/** Pins out, bus claimed, chip quiet. False when no chip answers. */
bool begin();

/** Whether begin() found a chip. */
bool present();

/** Unmodulated carrier on `ch`, full power, until stop() or powerDown(). */
void startConstCarrier(uint8_t ch);

/** Retune a running carrier. No-op when already there. */
void hopTo(uint8_t ch);

/** Carrier off, chip still powered. */
void stop();

/** Carrier off, chip powered down. Safe to call when it never started. */
void powerDown();

/** Channel the carrier is on, or 0xFF when it is not running. */
uint8_t channel();

}  // namespace Nrf24Raw
