#pragma once
// Private H.O.M.E chat codec: AES-128-CTR with a shared key, so only devices that
// carry the key can read the ESP-NOW/BLE/LoRa chat. Shared-secret ("obfuscation-
// grade") privacy, NOT PKI -- every H.O.M.E device has the same key baked in; it
// keeps outsiders off the net, it is not key-per-device security.
//
// THE SAME FILE MUST BE PRESENT (identical key) IN BOTH hud/ AND ESP32-DIV/ so the
// Combat HUD and the H.O.M.E firmware interoperate. Wire form of an encrypted chat
// frame: [16-byte random IV/nonce][CTR-ciphertext of the plaintext frame].
#include <stdint.h>
#include <string.h>
#include "mbedtls/aes.h"
#include "esp_random.h"

// 16-byte shared key ("HEADSOFMYENEMIES").
static const uint8_t HOME_CHAT_KEY[16] = {
  0x48, 0x45, 0x41, 0x44, 0x53, 0x4F, 0x46, 0x4D,
  0x59, 0x45, 0x4E, 0x45, 0x4D, 0x49, 0x45, 0x53
};

// Encrypt `len` bytes of `in` -> `out` as [IV(16)][cipher(len)]. out needs len+16.
// Returns total bytes written, or 0 on failure.
static inline int chat_encrypt(const uint8_t* in, int len, uint8_t* out) {
  if (len <= 0 || len > 200) return 0;
  for (int i = 0; i < 16; i++) out[i] = (uint8_t)esp_random();   // random IV
  uint8_t nonce[16]; memcpy(nonce, out, 16);
  uint8_t sb[16] = {0}; size_t nc = 0;
  mbedtls_aes_context a; mbedtls_aes_init(&a);
  int bad = mbedtls_aes_setkey_enc(&a, HOME_CHAT_KEY, 128)      // CTR uses the enc key both ways
         || mbedtls_aes_crypt_ctr(&a, len, &nc, nonce, sb, in, out + 16);
  mbedtls_aes_free(&a);
  return bad ? 0 : len + 16;
}

// Decrypt [IV(16)][cipher] of total `tot` -> `out` (tot-16 bytes). Returns plaintext
// length, or 0 on failure / too short.
static inline int chat_decrypt(const uint8_t* in, int tot, uint8_t* out) {
  if (tot <= 16) return 0;
  int len = tot - 16;
  uint8_t nonce[16]; memcpy(nonce, in, 16);
  uint8_t sb[16] = {0}; size_t nc = 0;
  mbedtls_aes_context a; mbedtls_aes_init(&a);
  int bad = mbedtls_aes_setkey_enc(&a, HOME_CHAT_KEY, 128)
         || mbedtls_aes_crypt_ctr(&a, len, &nc, nonce, sb, in + 16, out);
  mbedtls_aes_free(&a);
  return bad ? 0 : len;
}
