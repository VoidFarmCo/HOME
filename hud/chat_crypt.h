#pragma once
// Private H.O.M.E chat codec: per-NET keys derived from a passphrase (SHA-256), so
// groups each get their own key, plus an OPEN (unencrypted) broadcast net. A 2-byte
// NET TAG rides in the clear so a receiver knows which net a frame belongs to and
// whether/how to decrypt it. Shared-secret ("obfuscation-grade") privacy -- everyone
// in a net shares the passphrase; it keeps outsiders out, it is not per-device PKI.
//
// THE SAME FILE MUST BE PRESENT IN BOTH hud/ AND ESP32-DIV/ so the Combat HUD and the
// H.O.M.E firmware interoperate. Wire form of an ESP-NOW chat packet:
//   OPEN net:  [netId=0x0000][plaintext frame]
//   keyed net: [netId(2)][IV(16)][AES-128-CTR ciphertext of the frame]
#include <stdint.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "mbedtls/sha256.h"
#include "esp_random.h"

// Derive a 16-byte AES key from a passphrase (SHA-256, first 16 bytes).
static inline void chat_key_from_pass(const char* pass, uint8_t key[16]) {
  uint8_t h[32];
  mbedtls_sha256((const uint8_t*)pass, strlen(pass), h, 0);   // 0 = SHA-256
  memcpy(key, h, 16);
}
// 2-byte net tag from a key (what rides in the clear). OPEN is reserved 0x0000.
static inline uint16_t chat_net_id(const uint8_t key[16]) {
  uint16_t id = (uint16_t)((key[0] << 8) | key[1]);
  return id ? id : 0x0001;      // never collide with the OPEN tag
}

// Encrypt `len` bytes of `in` with `key` -> `out` = [IV(16)][cipher(len)]. out needs len+16.
static inline int chat_encrypt(const uint8_t key[16], const uint8_t* in, int len, uint8_t* out) {
  if (len <= 0 || len > 200) return 0;
  for (int i = 0; i < 16; i++) out[i] = (uint8_t)esp_random();
  uint8_t nonce[16]; memcpy(nonce, out, 16);
  uint8_t sb[16] = {0}; size_t nc = 0;
  mbedtls_aes_context a; mbedtls_aes_init(&a);
  int bad = mbedtls_aes_setkey_enc(&a, key, 128)             // CTR uses the enc key both ways
         || mbedtls_aes_crypt_ctr(&a, len, &nc, nonce, sb, in, out + 16);
  mbedtls_aes_free(&a);
  return bad ? 0 : len + 16;
}
// Decrypt [IV(16)][cipher] of total `tot` with `key` -> `out` (tot-16 bytes). Returns len or 0.
static inline int chat_decrypt(const uint8_t key[16], const uint8_t* in, int tot, uint8_t* out) {
  if (tot <= 16) return 0;
  int len = tot - 16;
  uint8_t nonce[16]; memcpy(nonce, in, 16);
  uint8_t sb[16] = {0}; size_t nc = 0;
  mbedtls_aes_context a; mbedtls_aes_init(&a);
  int bad = mbedtls_aes_setkey_enc(&a, key, 128)
         || mbedtls_aes_crypt_ctr(&a, len, &nc, nonce, sb, in + 16, out);
  mbedtls_aes_free(&a);
  return bad ? 0 : len;
}
