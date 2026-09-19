#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Eapol — finding the 802.1X payload inside an 802.11 data frame.
 *
 * Step 2 of docs/pueo/eapol-capture.md. This locates; it does not yet
 * classify, record, or transmit anything.
 *
 * The job is small and the way to get it wrong is well known. ESP32Marauder
 * (MIT, Copyright (c) 2020 Just Call Me Koko) tests two fixed offsets for
 * the ethertype:
 *
 *     payload[30] == 0x88 && payload[31] == 0x8e   ->  24-byte header
 *     payload[32] == 0x88 && payload[33] == 0x8e   ->  26-byte QoS header
 *
 * which reads four bytes without checking the frame is that long, and knows
 * only those two layouts. A header can also carry a fourth address, or an HT
 * Control field, and then the ethertype is somewhere else entirely.
 *
 * The length is computable from the frame control field, so it is computed,
 * and every read is bounded first. Same discipline as the information
 * element walk in Spotter, for the same reason: this is a buffer off the
 * air and the numbers in it are somebody else's to choose.
 *
 * tools/check_eapol_locate.py holds the arithmetic to account.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Eapol {

/**
 * Length of the 802.11 header for this frame, or -1 when the frame is not a
 * data frame carrying a payload, or is too short to tell.
 *
 * Exposed separately from findPayload because it is the part worth testing
 * on its own.
 */
int headerLength(const uint8_t* frame, uint16_t len);

/**
 * Offset of the 802.1X payload, or -1 when this frame does not carry one.
 *
 * Requires a data frame, unprotected, with an LLC/SNAP header declaring
 * ethertype 0x888E. Protected frames are rejected rather than parsed: the
 * body is ciphertext. The 4-way handshake itself runs before either side has
 * installed a key, so the frames worth having are in the clear.
 */
int findPayload(const uint8_t* frame, uint16_t len);

}  // namespace Eapol
