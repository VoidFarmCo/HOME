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

/** Which message of the 4-way handshake a frame carries. */
enum class Msg : uint8_t { None = 0, M1, M2, M3, M4 };

/**
 * Classify the EAPOL-Key frame whose 802.1X payload begins at `eapolOffset`,
 * which comes from findPayload().
 *
 * Only EAPOL-Key packets are considered; EAP, Start and Logoff share the
 * ethertype and carry no Key Information field. Three bits of that field
 * separate the four messages -- see the table in Eapol.cpp.
 */
Msg classify(const uint8_t* frame, uint16_t len, int eapolOffset);

/**
 * Point `bssid` and `station` at the right addresses for this frame.
 *
 * Which of the three addresses is which depends on the DS bits, so it is
 * read off them rather than guessed. Returns false for a WDS frame, where
 * neither question has a single answer, and for a frame too short to hold
 * three addresses.
 */
bool addresses(const uint8_t* frame, uint16_t len,
               const uint8_t** bssid, const uint8_t** station);

/* ── what has been seen, per access point ────────────────────────────────
 *
 * Kept per AP rather than per frame, because the useful question is whether
 * there is a usable handshake for a network, not how many EAPOL frames went
 * past. Nothing here writes to a card or draws anything; that is step 4.
 */
constexpr int kMaxHandshakes = 16;

struct Handshake {
  uint8_t  bssid[6];
  uint8_t  station[6];
  uint8_t  seen;      // bit 0 = M1, bit 1 = M2, bit 2 = M3, bit 3 = M4
  uint32_t firstMs;
  uint32_t lastMs;
};

/** Mask for `Handshake::seen`. Msg::None maps to 0. */
uint8_t maskOf(Msg m);

/**
 * Offer a frame to the tracker. Does nothing unless it is an EAPOL-Key
 * frame that classifies. Returns the message it was, so a caller can react
 * without parsing again.
 */
Msg observe(const uint8_t* frame, uint16_t len, uint32_t nowMs);

int              handshakeCount();
const Handshake* handshakeAt(int i);
void             resetHandshakes();

/** True when M2 and M3 are both present, which is the pair worth having. */
bool usable(const Handshake& h);

}  // namespace Eapol
