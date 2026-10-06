#include "mesh.h"
#include <string.h>
#include <stdlib.h>
#include "mbedtls/aes.h"

namespace Mesh {

/* Pre-shared H.O.M.E network key (16 bytes, AES-128). Baked in: a closed-net
 * key, not a per-user secret. Change it to fork a private net off the public one.
 * NOT all-zero / not an obvious example value (check_lora_mesh.py enforces that). */
static const uint8_t HOME_KEY[16] = {
  0x48, 0x4F, 0x4D, 0x45, 0x9A, 0x3C, 0x71, 0xE2,
  0x05, 0xB8, 0x6D, 0x14, 0xCF, 0x27, 0x90, 0xAB,
};

static uint16_t s_myId = 0;
static uint16_t s_nextMsgId = 1;

/* Seen-cache: a small ring of (src,id) so a flooded packet is relayed/shown once. */
static constexpr int SEEN_N = 24;
struct Seen { uint16_t src; uint16_t id; bool used; };
static Seen* s_seen = nullptr;   // heap: lives only while a mesh session runs
static int   s_seenHead = 0;

static bool seenHas(uint16_t src, uint16_t id) {
  if (!s_seen) return false;
  for (int i = 0; i < SEEN_N; i++)
    if (s_seen[i].used && s_seen[i].src == src && s_seen[i].id == id) return true;
  return false;
}
static void seenAdd(uint16_t src, uint16_t id) {
  if (!s_seen) return;
  s_seen[s_seenHead] = { src, id, true };
  s_seenHead = (s_seenHead + 1) % SEEN_N;
}

void begin(uint16_t myId) {
  s_myId = myId;
  s_nextMsgId = 1;
  if (!s_seen) s_seen = (Seen*)calloc(SEEN_N, sizeof(Seen));
  if (s_seen) memset(s_seen, 0, SEEN_N * sizeof(Seen));
  s_seenHead = 0;
}

/* AES-128-CTR, in place. The counter block is derived only from (src,id) so it
 * is identical on the originator and every relay -- relays never re-encrypt. */
static void crypt(uint16_t src, uint16_t id, uint8_t* data, int len) {
  uint8_t nonce[16];
  memset(nonce, 0, sizeof(nonce));
  nonce[0] = (uint8_t)(src >> 8); nonce[1] = (uint8_t)(src & 0xFF);
  nonce[2] = (uint8_t)(id  >> 8); nonce[3] = (uint8_t)(id  & 0xFF);
  uint8_t stream[16];
  size_t ncOff = 0;
  memset(stream, 0, sizeof(stream));
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  mbedtls_aes_setkey_enc(&aes, HOME_KEY, 128);
  mbedtls_aes_crypt_ctr(&aes, (size_t)len, &ncOff, nonce, stream, data, data);
  mbedtls_aes_free(&aes);
}

int wrap(const uint8_t* payload, uint8_t plen, uint8_t* out, int outCap) {
  if (plen > MESH_MAX_PAYLOAD) plen = MESH_MAX_PAYLOAD;
  const int total = MESH_HEADER + plen;
  if (total > outCap) return 0;
  const uint16_t id = s_nextMsgId++;
  out[0] = MESH_MAGIC;
  out[1] = 0x01;
  out[2] = (uint8_t)(s_myId >> 8); out[3] = (uint8_t)(s_myId & 0xFF);
  out[4] = (uint8_t)(id >> 8);     out[5] = (uint8_t)(id & 0xFF);
  out[6] = MESH_DEFAULT_HOPS;
  out[7] = plen;
  memcpy(out + MESH_HEADER, payload, plen);
  crypt(s_myId, id, out + MESH_HEADER, plen);
  seenAdd(s_myId, id);   // don't act on our own echo
  return total;
}

int unwrap(const uint8_t* pkt, int len,
           uint8_t* payloadOut, int payloadCap,
           uint8_t* relayBuf, int* relayLen, int relayCap) {
  if (relayLen) *relayLen = 0;
  if (len < MESH_HEADER) return 0;
  if (pkt[0] != MESH_MAGIC || pkt[1] != 0x01) return 0;
  const uint16_t src = (uint16_t)((pkt[2] << 8) | pkt[3]);
  const uint16_t id  = (uint16_t)((pkt[4] << 8) | pkt[5]);
  const uint8_t  hops = pkt[6];
  const uint8_t  plen = pkt[7];
  if (MESH_HEADER + plen > len || plen > payloadCap) return 0;
  if (src == s_myId) return 0;        // our own echo
  if (seenHas(src, id)) return 0;     // duplicate from the flood
  seenAdd(src, id);

  // Relay: forward with one fewer hop, ciphertext untouched.
  if (relayBuf && hops > 1 && relayLen) {
    const int total = MESH_HEADER + plen;
    if (total <= relayCap) {
      memcpy(relayBuf, pkt, total);
      relayBuf[6] = (uint8_t)(hops - 1);
      *relayLen = total;
    }
  }

  // Decrypt a copy for display.
  memcpy(payloadOut, pkt + MESH_HEADER, plen);
  crypt(src, id, payloadOut, plen);
  return plen;
}

}  // namespace Mesh
