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

/* ── the passive side ────────────────────────────────────────────────────
 * The same chip will tell you whether anything else is on air, which is a
 * more useful thing to do with it than transmitting.
 *
 * What it gives you is the RPD: one bit, latched while the receiver is on,
 * set when the channel carried more than about -64 dBm. Not a level, not a
 * spectrum, and not calibrated. Sample a channel repeatedly and count the
 * ones and you have an occupancy figure; that is the whole instrument.
 *
 * -64 dBm is high. A strong nearby transmitter shows and a weak distant one
 * does not, so an empty result means "nothing loud here" rather than
 * "nothing here". Worth remembering before reading a flat sweep as quiet.
 * ───────────────────────────────────────────────────────────────────────── */

/** Receiver on for RPD sampling. Stops any carrier first. */
void startReceiver();

/** True when RPD latched on `ch`. Dwell is on top of the 130 us settle. */
bool sampleRpd(uint8_t ch, uint16_t dwellUs = 40);

/** Receiver off, chip still powered. */
void stopReceiver();

/* The band, in this chip's own channel numbering: centre = 2400 + n MHz.
 *
 * RF_CH takes 0..125, which reaches 2525 and runs 41 MHz past the top of
 * the ISM allocation. A sweep has no business up there, so these are the
 * limits every caller should use rather than the register's range. */
constexpr uint8_t kChanMin = 0;    // 2400 MHz
constexpr uint8_t kChanMax = 83;   // 2483 MHz, ISM ends at 2483.5

}  // namespace Nrf24Raw
