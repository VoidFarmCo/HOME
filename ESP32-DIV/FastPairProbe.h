#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * FastPairProbe — testing a Fast Pair provider for CVE-2025-36911.
 *
 * WHAT THIS TRANSMITS, because that is the first thing a reader should know:
 * it opens a GATT connection to one device you select and writes eighty
 * bytes to one characteristic. That is all. It does not pair, does not
 * write an account key, does not deauthenticate anything and does not
 * broadcast. But it is unambiguously an active test against a specific
 * piece of somebody's hardware, so point it at your own devices or ones you
 * have permission to test. See docs/pueo/fast-pair-probe.md.
 *
 * ── The normal handshake ────────────────────────────────────────────────────
 *
 * A Fast Pair Seeker that wants to pair with a Provider:
 *
 *   1. looks up the Provider's anti-spoofing public key on Google's servers,
 *      keyed by the Model ID in the advertisement;
 *   2. generates an ephemeral secp256r1 keypair;
 *   3. does ECDH against the anti-spoofing key, SHA-256s the shared X
 *      coordinate and keeps the first 16 bytes as an AES key;
 *   4. AES-128-ECB encrypts a 16-byte request that names the Provider's own
 *      BLE address;
 *   5. writes those 16 bytes plus its own 64-byte public key to the
 *      Key-based Pairing characteristic.
 *
 * The Provider does the same ECDH with its anti-spoofing *private* key,
 * gets the same AES key, decrypts, and checks that the address in the
 * request is its own. Only then does it notify a response.
 *
 * The address check is the whole security property. A Seeker that did not
 * get the real anti-spoofing key from Google derives a different AES key,
 * so its request decrypts to noise, so the address field is noise, so it
 * does not match, so a correct Provider stays silent.
 *
 * ── What this probe does ────────────────────────────────────────────────────
 *
 * Exactly the above, except at step 1 there is no anti-spoofing key,
 * because that is the point. The key used is either the Provider's own
 * public key as read from the Model ID characteristic, or an ephemeral one
 * of ours -- neither of which is the anti-spoofing key. The request is
 * otherwise well formed and names the Provider's address correctly.
 *
 * A correct Provider must not answer. A notification means the address
 * check did not happen, or happened against something the Seeker controls.
 *
 * ── What a result does and does not mean ────────────────────────────────────
 *
 * A response is evidence, not proof, and silence is much weaker evidence
 * than it looks. A device can stay quiet because it is busy, because it is
 * already connected to its owner's phone, because it dropped the link, or
 * because the characteristic was not where this looked. That is why the
 * result is a four-way Outcome and not a red VULNERABLE banner, and why
 * NoResponse is spelled "no response" rather than "not vulnerable".
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include <stdint.h>

namespace FastPairProbe {

/* The Fast Pair GATT characteristics, all under service 0xFE2C. */
extern const char* const kUuidService;
extern const char* const kUuidKeyBasedPairing;
extern const char* const kUuidModelId;

/** Message types at byte 0 of a decrypted block. */
static constexpr uint8_t kMsgKbpRequest  = 0x00;
static constexpr uint8_t kMsgKbpResponse = 0x01;

/* Flag bits at byte 1 of a request. */
static constexpr uint8_t kFlagDiscoverable       = 0x80;
static constexpr uint8_t kFlagInitiateBonding    = 0x40;
static constexpr uint8_t kFlagRetroactiveWrite   = 0x10;

/**
 * Build the 16-byte plaintext of a Key-based Pairing Request.
 *
 * `providerAddr` is the six bytes of the target's BLE address, most
 * significant first, as they go on the wire. `seekerAddr` may be null, in
 * which case the field is filled from `salt` like the rest of the tail.
 * `salt` must supply 8 bytes; only as many as are needed are used.
 *
 * Pure: no radio, no crypto, no allocation. tools/check_fastpair_probe.py
 * checks it byte for byte.
 */
void buildRequest(uint8_t flags,
                  const uint8_t providerAddr[6],
                  const uint8_t* seekerAddr,   // 6 bytes, or nullptr
                  const uint8_t salt[8],
                  uint8_t out[16]);

/**
 * Check whether a decrypted 16-byte block looks like a Key-based Pairing
 * Response naming `expectAddr`.
 *
 * A response is type 0x01 then the Provider's six-byte public address then
 * nine bytes of salt. `addrMatches` is set when that address equals
 * `expectAddr`, which distinguishes a device that genuinely processed the
 * request from one that echoed noise.
 */
bool parseResponse(const uint8_t block[16],
                   const uint8_t expectAddr[6],
                   bool* addrMatches);

/** How the probe ended. */
enum class Outcome : uint8_t {
  NotRun = 0,
  Responded,     // a notification arrived: the address check did not hold
  NoResponse,    // nothing came back before the timeout -- see the header
  NoService,     // no 0xFE2C service or no Key-based Pairing characteristic
  Failed,        // could not connect, write, or set up crypto
};

/** Everything one probe produced, for the UI and the log. */
struct Report {
  Outcome  outcome;
  bool     responseWellFormed;  // decrypted to type 0x01
  bool     responseAddrMatched; // and named the provider's own address
  uint16_t elapsedMs;
  uint8_t  request[16];         // plaintext, before encryption
  uint8_t  notified[16];        // raw bytes notified back, if any
  uint8_t  notifiedLen;
  char     detail[48];          // a phrase for the screen
};

/**
 * Run one probe against `addr` (six bytes, most significant first).
 *
 * `isPublic` picks the address type. `timeoutMs` bounds the wait for a
 * notification. Blocks for up to roughly `timeoutMs` plus connection time.
 */
void run(const uint8_t addr[6], bool isPublic, uint16_t timeoutMs,
         Report* out);

/** A short phrase for an outcome, for putting on screen. */
const char* outcomeText(Outcome o);

}  // namespace FastPairProbe
