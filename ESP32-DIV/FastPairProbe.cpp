#include "FastPairProbe.h"

#include <string.h>

#include "BleCompat.h"

#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/ecdh.h"
#include "mbedtls/ecp.h"
#include "mbedtls/entropy.h"
#include "mbedtls/sha256.h"

namespace FastPairProbe {

/* Fast Pair's characteristics. The 0xFE2C in the first four hex digits is
 * the same 16-bit UUID the advertisement uses, widened to 128 bits by the
 * usual Bluetooth base-UUID rule; the rest is Google's. */
const char* const kUuidService          = "0000fe2c-0000-1000-8000-00805f9b34fb";
const char* const kUuidKeyBasedPairing  = "fe2c1234-8366-4814-8eb0-01de32100bea";
const char* const kUuidModelId          = "fe2c1233-8366-4814-8eb0-01de32100bea";

/* ── The pure part ───────────────────────────────────────────────────────── */

void buildRequest(uint8_t flags,
                  const uint8_t providerAddr[6],
                  const uint8_t* seekerAddr,
                  const uint8_t salt[8],
                  uint8_t out[16]) {
  if (out == nullptr) {
    return;
  }
  memset(out, 0, 16);
  out[0] = kMsgKbpRequest;
  out[1] = flags;
  if (providerAddr != nullptr) {
    memcpy(out + 2, providerAddr, 6);
  }
  if (seekerAddr != nullptr) {
    /* The Seeker's address occupies bytes 8..13, and only two bytes of salt
     * follow it. This is the shape a Seeker sends when it wants the
     * Provider to initiate bonding back to it. */
    memcpy(out + 8, seekerAddr, 6);
    if (salt != nullptr) {
      memcpy(out + 14, salt, 2);
    }
  } else if (salt != nullptr) {
    /* No Seeker address: the whole tail is salt. */
    memcpy(out + 8, salt, 8);
  }
}

bool parseResponse(const uint8_t block[16],
                   const uint8_t expectAddr[6],
                   bool* addrMatches) {
  if (addrMatches != nullptr) {
    *addrMatches = false;
  }
  if (block == nullptr || block[0] != kMsgKbpResponse) {
    return false;
  }
  if (addrMatches != nullptr && expectAddr != nullptr) {
    *addrMatches = (memcmp(block + 1, expectAddr, 6) == 0);
  }
  return true;
}

const char* outcomeText(Outcome o) {
  switch (o) {
    case Outcome::NotRun:     return "not run";
    case Outcome::Responded:  return "responded";
    case Outcome::NoResponse: return "no response";
    case Outcome::NoService:  return "no Fast Pair service";
    case Outcome::Failed:     return "probe failed";
  }
  return "unknown";
}

/* ── The part that needs a radio ─────────────────────────────────────────── */

namespace {

volatile bool s_notified = false;
uint8_t s_notifyBuf[16];
uint8_t s_notifyLen = 0;

void onNotify(NimBLERemoteCharacteristic* /*chr*/, uint8_t* data, size_t len,
              bool /*isNotify*/) {
  if (s_notified) {
    return;                             // keep the first, ignore the rest
  }
  const size_t n = (len > sizeof(s_notifyBuf)) ? sizeof(s_notifyBuf) : len;
  memcpy(s_notifyBuf, data, n);
  s_notifyLen = (uint8_t)n;
  s_notified = true;
}

/* Derive the AES key the way a Seeker would: ECDH on secp256r1, SHA-256 of
 * the shared X coordinate, first 16 bytes.
 *
 * `peerPub` is 64 bytes, X then Y, which is the uncompressed point without
 * its 0x04 prefix -- the form Fast Pair puts on the wire. On success
 * `ourPubOut` receives our own 64 bytes in the same form. */
bool deriveKey(const uint8_t peerPub[64], uint8_t ourPubOut[64],
               uint8_t keyOut[16]) {
  bool ok = false;
  mbedtls_ecp_group grp;
  mbedtls_mpi ourPriv;
  mbedtls_ecp_point peer, shared, ourPubPoint;
  mbedtls_entropy_context entropy;
  mbedtls_ctr_drbg_context drbg;

  mbedtls_ecp_group_init(&grp);
  mbedtls_mpi_init(&ourPriv);
  mbedtls_ecp_point_init(&peer);
  mbedtls_ecp_point_init(&shared);
  mbedtls_ecp_point_init(&ourPubPoint);
  mbedtls_entropy_init(&entropy);
  mbedtls_ctr_drbg_init(&drbg);

  do {
    static const char kPers[] = "pueo-fastpair";
    if (mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                              (const unsigned char*)kPers,
                              sizeof(kPers) - 1) != 0) {
      break;
    }
    if (mbedtls_ecp_group_load(&grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
      break;
    }
    /* Our ephemeral keypair. */
    if (mbedtls_ecp_gen_keypair(&grp, &ourPriv, &ourPubPoint,
                                mbedtls_ctr_drbg_random, &drbg) != 0) {
      break;
    }
    if (mbedtls_mpi_write_binary(&ourPubPoint.X,
                                 ourPubOut, 32) != 0 ||
        mbedtls_mpi_write_binary(&ourPubPoint.Y,
                                 ourPubOut + 32, 32) != 0) {
      break;
    }

    /* The peer point, rebuilt from its 64 bytes. */
    if (mbedtls_mpi_read_binary(&peer.X, peerPub, 32) != 0 ||
        mbedtls_mpi_read_binary(&peer.Y,
                                peerPub + 32, 32) != 0 ||
        mbedtls_mpi_lset(&peer.Z, 1) != 0) {
      break;
    }
    if (mbedtls_ecp_check_pubkey(&grp, &peer) != 0) {
      break;                            // not a point on the curve
    }

    if (mbedtls_ecp_mul(&grp, &shared, &ourPriv, &peer,
                        mbedtls_ctr_drbg_random, &drbg) != 0) {
      break;
    }
    uint8_t x[32];
    if (mbedtls_mpi_write_binary(&shared.X, x,
                                 sizeof(x)) != 0) {
      break;
    }
    uint8_t digest[32];
    if (mbedtls_sha256_ret(x, sizeof(x), digest, 0) != 0) {
      break;
    }
    memcpy(keyOut, digest, 16);
    ok = true;
  } while (false);

  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&entropy);
  mbedtls_ecp_point_free(&ourPubPoint);
  mbedtls_ecp_point_free(&shared);
  mbedtls_ecp_point_free(&peer);
  mbedtls_mpi_free(&ourPriv);
  mbedtls_ecp_group_free(&grp);
  return ok;
}

/* The secp256r1 generator, as 64 bytes of X then Y.
 *
 * Used as the peer point when the Provider does not hand us one. It is a
 * valid point on the curve, so the ECDH is well formed and the request is
 * well formed; it is simply not the Provider's anti-spoofing key, which is
 * the entire point of the test. Named for what it is rather than dressed up
 * as a key, because it is not one. */
const uint8_t kGeneratorP256[64] = {
  0x6B, 0x17, 0xD1, 0xF2, 0xE1, 0x2C, 0x42, 0x47,
  0xF8, 0xBC, 0xE6, 0xE5, 0x63, 0xA4, 0x40, 0xF2,
  0x77, 0x03, 0x7D, 0x81, 0x2D, 0xEB, 0x33, 0xA0,
  0xF4, 0xA1, 0x39, 0x45, 0xD8, 0x98, 0xC2, 0x96,
  0x4F, 0xE3, 0x42, 0xE2, 0xFE, 0x1A, 0x7F, 0x9B,
  0x8E, 0xE7, 0xEB, 0x4A, 0x7C, 0x0F, 0x9E, 0x16,
  0x2B, 0xCE, 0x33, 0x57, 0x6B, 0x31, 0x5E, 0xCE,
  0xCB, 0xB6, 0x40, 0x68, 0x37, 0xBF, 0x51, 0xF5
};

bool aesEcbEncrypt(const uint8_t key[16], const uint8_t in[16],
                   uint8_t out[16]) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  bool ok = (mbedtls_aes_setkey_enc(&aes, key, 128) == 0) &&
            (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_ENCRYPT, in, out) == 0);
  mbedtls_aes_free(&aes);
  return ok;
}

bool aesEcbDecrypt(const uint8_t key[16], const uint8_t in[16],
                   uint8_t out[16]) {
  mbedtls_aes_context aes;
  mbedtls_aes_init(&aes);
  bool ok = (mbedtls_aes_setkey_dec(&aes, key, 128) == 0) &&
            (mbedtls_aes_crypt_ecb(&aes, MBEDTLS_AES_DECRYPT, in, out) == 0);
  mbedtls_aes_free(&aes);
  return ok;
}

void say(Report* r, const char* text) {
  snprintf(r->detail, sizeof(r->detail), "%s", text);
}

}  // namespace

void run(const uint8_t addr[6], bool isPublic, uint16_t timeoutMs,
         Report* out) {
  if (out == nullptr || addr == nullptr) {
    return;
  }
  memset(out, 0, sizeof(*out));
  out->outcome = Outcome::Failed;
  say(out, "starting");

  const uint32_t startMs = millis();
  s_notified = false;
  s_notifyLen = 0;

  /* `addr` is in written order, AA:BB:CC:DD:EE:FF, which is the order the
   * request field at bytes 2..7 wants and the order the scanner hands over.
   * NimBLEAddress stores little-endian internally but its six-byte
   * constructor reverse-copies, so it takes written order too. The two
   * agree; getting this wrong would connect to a reversed address, every
   * probe would time out, and a broken probe is indistinguishable from a
   * room full of correctly-behaving devices. */
  NimBLEAddress target(const_cast<uint8_t*>(addr),
                       isPublic ? BLE_ADDR_PUBLIC : BLE_ADDR_RANDOM);

  NimBLEClient* client = NimBLEDevice::createClient();
  if (client == nullptr) {
    say(out, "no client");
    return;
  }
  client->setConnectTimeout(5);

  do {
    if (!client->connect(target, true)) {
      out->outcome = Outcome::Failed;
      say(out, "connect failed");
      break;
    }

    NimBLERemoteService* svc = client->getService(kUuidService);
    if (svc == nullptr) {
      out->outcome = Outcome::NoService;
      say(out, "no 0xFE2C service");
      break;
    }
    NimBLERemoteCharacteristic* kbp =
        svc->getCharacteristic(kUuidKeyBasedPairing);
    if (kbp == nullptr) {
      out->outcome = Outcome::NoService;
      say(out, "no KBP characteristic");
      break;
    }

    /* The key this derives against is deliberately not the anti-spoofing
     * key, which only Google serves and which we do not have. If the
     * Provider exposes a public key on its Model ID characteristic we use
     * that; otherwise we ECDH against a second ephemeral point of our own.
     * Either way the Provider cannot arrive at the same AES key by the
     * intended route, so a correct one will not answer. */
    uint8_t peerPub[64];
    bool havePeer = false;
    NimBLERemoteCharacteristic* midChr = svc->getCharacteristic(kUuidModelId);
    if (midChr != nullptr && midChr->canRead()) {
      const std::string v = midChr->readValue();
      if (v.size() >= 64) {
        memcpy(peerPub, v.data(), 64);
        havePeer = true;
      }
    }
    if (!havePeer) {
      memcpy(peerPub, kGeneratorP256, sizeof(peerPub));
    }

    uint8_t ourPub[64];
    uint8_t aesKey[16];
    if (!deriveKey(peerPub, ourPub, aesKey)) {
      out->outcome = Outcome::Failed;
      say(out, "ECDH failed");
      break;
    }

    uint8_t salt[8];
    for (size_t i = 0; i < sizeof(salt); i++) {
      salt[i] = (uint8_t)random(256);
    }
    buildRequest(kFlagDiscoverable, addr, nullptr, salt, out->request);

    uint8_t encrypted[16];
    if (!aesEcbEncrypt(aesKey, out->request, encrypted)) {
      out->outcome = Outcome::Failed;
      say(out, "AES failed");
      break;
    }

    if (kbp->canNotify()) {
      kbp->subscribe(true, onNotify);
    }

    /* 16 bytes of request, then our 64-byte public key, which is what the
     * Provider would need to do the ECDH from its side. */
    uint8_t packet[80];
    memcpy(packet, encrypted, 16);
    memcpy(packet + 16, ourPub, 64);
    if (!kbp->writeValue(packet, sizeof(packet), true)) {
      out->outcome = Outcome::Failed;
      say(out, "write rejected");
      break;
    }

    const uint32_t deadline = millis() + timeoutMs;
    while (!s_notified && (int32_t)(deadline - millis()) > 0) {
      delay(10);
    }

    if (!s_notified) {
      out->outcome = Outcome::NoResponse;
      say(out, "silent (see docs)");
      break;
    }

    memcpy(out->notified, s_notifyBuf, sizeof(out->notified));
    out->notifiedLen = s_notifyLen;
    out->outcome = Outcome::Responded;

    /* Decrypting with our key only succeeds if the Provider used it too,
     * which is the strong form of the finding. A notification that does
     * not decrypt is still a notification the device should not have sent,
     * so it is reported either way and the two are distinguished. */
    uint8_t plain[16];
    if (out->notifiedLen >= 16 && aesEcbDecrypt(aesKey, out->notified, plain)) {
      bool matched = false;
      out->responseWellFormed = parseResponse(plain, addr, &matched);
      out->responseAddrMatched = matched;
    }
    if (out->responseAddrMatched) {
      say(out, "response, address matched");
    } else if (out->responseWellFormed) {
      say(out, "response, address differed");
    } else {
      say(out, "notified, did not decrypt");
    }
  } while (false);

  if (client->isConnected()) {
    client->disconnect();
  }
  NimBLEDevice::deleteClient(client);

  const uint32_t elapsed = millis() - startMs;
  out->elapsedMs = (uint16_t)((elapsed > 0xFFFF) ? 0xFFFF : elapsed);
}

}  // namespace FastPairProbe
