#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * mesh — the private H.O.M.E LoRa mesh.
 *
 * This is OURS, not Meshtastic firmware (only the SX1262 radio is Meshtastic-
 * compatible). A tiny flood mesh: every node rebroadcasts a message it has not
 * seen until a hop limit runs out, and a seen-cache of (source, id) kills the
 * duplicates a flood creates. The chat payload is AES-128-CTR encrypted under a
 * shared H.O.M.E key, so only our units read it, and the counter is derived from
 * (source, id) -- stable across relays, which never touch the ciphertext.
 *
 * Wire packet (header plaintext, payload encrypted):
 *   [0]  magic 'H' (0x48)      [1]  version 0x01
 *   [2..3] srcId (u16, big-E)  [4..5] msgId (u16, big-E)
 *   [6]  hopsLeft              [7]  payloadLen
 *   [8..] ciphertext (payloadLen bytes)
 *
 * The pre-shared key is baked into the firmware: obfuscation-grade privacy for a
 * closed net, NOT per-user secrecy. That is the honest scope.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Mesh {

constexpr int MESH_HEADER = 8;
constexpr int MESH_MAX_PAYLOAD = 64;
constexpr int MESH_MAX_PACKET = MESH_HEADER + MESH_MAX_PAYLOAD;   // 72
constexpr uint8_t MESH_MAGIC = 0x48;      // 'H'
constexpr uint8_t MESH_DEFAULT_HOPS = 3;  // flood radius

/* Set this node's id (e.g. low 16 bits of the MAC) and clear the seen-cache. */
void begin(uint16_t myId);

/* Wrap a payload as a NEW mesh packet (fresh msgId, full hop limit), encrypting
 * it. Returns the packet length written to out, or 0 if it will not fit. */
int wrap(const uint8_t* payload, uint8_t plen, uint8_t* out, int outCap);

/* Handle a received raw packet.
 *  - Returns the decrypted payload length (>0) in payloadOut when the packet is
 *    NEW (not a duplicate, valid header) and should be shown; 0 otherwise.
 *  - When the message should be forwarded (new, from someone else, hops remain),
 *    writes the hop-decremented packet to relayBuf and sets *relayLen; otherwise
 *    *relayLen = 0. Pass relayBuf=nullptr to skip relay handling.
 * A node never relays or displays its own echoed packets. */
int unwrap(const uint8_t* pkt, int len,
           uint8_t* payloadOut, int payloadCap,
           uint8_t* relayBuf, int* relayLen, int relayCap);

}  // namespace Mesh
