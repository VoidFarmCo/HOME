#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * FastPair — reading Google Fast Pair advertisements.
 *
 * The Apple side of this device is well covered: Continuity proximity
 * pairing, nearby action and offline finding are parsed in three places.
 * The Google side was not covered at all. The only 0xFE2C in this tree was
 * a spoofing template, and it was dead code.
 *
 * Fast Pair puts its payload in BLE *service data* under 16-bit UUID 0xFE2C,
 * not in manufacturer data, which is why an Apple-shaped parser never saw
 * it. The payload has two shapes and they are easy to confuse:
 *
 *   Discoverable (in pairing mode)
 *       Exactly three bytes: a 24-bit Model ID. The device is asking to be
 *       paired and is announcing what kind of thing it is.
 *
 *   Not-discoverable (already paired to somebody)
 *       A version/flags byte, then an account key filter, then optional
 *       salt and battery fields. No Model ID anywhere in it.
 *
 * Most devices in the air are in the second state, because most earbuds
 * belong to somebody already. Reading the first three bytes of a
 * not-discoverable frame as a Model ID -- which is what a naive parser
 * does -- yields a number that is not a Model ID and will happily print as
 * one. The length check that separates the two is the whole job.
 *
 * On identity, which is the reason to be careful here:
 *
 *   A Model ID identifies a *model*, not a unit. 0x2B71B2 means "one of
 *   these", not "this one". Two people with the same earbuds advertise the
 *   same Model ID.
 *
 *   An account key filter is a bloom filter over the device's account keys,
 *   salted, and the salt rotates. That is deliberate: it exists so that a
 *   passive listener cannot follow the device. It is not a stable
 *   identifier and treating it as one will not work.
 *
 * So Fast Pair, implemented correctly, hands a passive listener no stable
 * per-unit identifier at all. Anything that claims to track a Fast Pair
 * device across address rotation is either matching on a model (and merging
 * strangers' devices together) or matching on a name. Both are recorded in
 * docs/pueo/fast-pair.md, because the temptation to do it is real.
 *
 * This parses bytes and nothing else: no BLE stack, no display, no radio.
 * That is what lets tools/check_fastpair.py run it on a host, and the input
 * is a packet from the air, so every field is bounded before it is read.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include <stdint.h>

namespace FastPair {

/** The 16-bit service UUIDs this concerns. */
static constexpr uint16_t kUuidFastPair = 0xFE2C;
static constexpr uint16_t kUuidNearby   = 0xFEF3;

enum class Frame : uint8_t {
  None = 0,      // not a Fast Pair payload, or malformed
  ModelId,       // discoverable: three bytes, device is in pairing mode
  AccountKey,    // not-discoverable: an account key filter, already paired
  Empty,         // not-discoverable with no account keys advertised
};

/** Battery, when the device chooses to advertise it. */
struct Battery {
  bool    present;
  bool    hideUi;      // the device asked for this not to be shown
  uint8_t count;       // how many components were reported, 0-3
  uint8_t level[3];    // percent 0-100, or kLevelUnknown
  bool    charging[3];
};

static constexpr uint8_t kLevelUnknown = 0xFF;

/** The most a bounded parse will keep of an account key filter. */
static constexpr uint8_t kMaxFilter = 16;

struct Adv {
  Frame    frame;
  /* ModelId frames only. */
  uint32_t modelId;        // 24-bit
  /* AccountKey / Empty frames only. */
  uint8_t  version;        // upper nibble of the flags byte
  uint8_t  flags;          // lower nibble
  uint8_t  filterLen;      // bytes actually stored in filter[]
  bool     filterTruncated;// the advertised filter was longer than kMaxFilter
  bool     filterHideUi;   // filter type asked for no UI indication
  uint8_t  filter[kMaxFilter];
  bool     haveSalt;
  uint16_t salt;
  Battery  battery;
};

/**
 * Parse `len` bytes of 0xFE2C service data into `out`.
 *
 * Returns false and sets `out->frame` to Frame::None when the payload is not
 * a shape this understands. `out` is zeroed either way.
 */
bool parse(const uint8_t* data, size_t len, Adv* out);

/**
 * A human name for a Model ID, or nullptr when it is not known.
 *
 * Almost always nullptr. See kModels in FastPair.cpp for why the table is
 * empty rather than guessed at, and print the hex ID when this returns
 * nothing.
 */
const char* modelName(uint32_t modelId);

/** A short phrase for a frame kind, for putting on screen. */
const char* frameText(Frame f);

}  // namespace FastPair
