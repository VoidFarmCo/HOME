#include "FastPair.h"

#include <string.h>

namespace FastPair {

namespace {

/* Field types inside a not-discoverable frame. Each field is a header byte
 * -- length in the upper nibble, type in the lower -- then that many bytes. */
constexpr uint8_t kTypeFilterShowUi = 0x0;
constexpr uint8_t kTypeFilterHideUi = 0x2;
constexpr uint8_t kTypeSalt         = 0x1;
constexpr uint8_t kTypeBatteryShowUi = 0x3;
constexpr uint8_t kTypeBatteryHideUi = 0x4;

/* Model ID to name.
 *
 * Empty, and that is a decision rather than an omission.
 *
 * Google does publish the mapping -- the Nearby Devices metadata service
 * resolves a Model ID to a product name -- but it needs an API key, so
 * nothing here can check an entry at build time or at read time. The lists
 * that circulate for ESP32 and Flipper "Fast Pair spam" tools are lists of
 * IDs that *provoke a popup*, and the names beside them are frequently
 * wrong, because provoking a popup does not require the name to be right.
 * Copying one in would put a confident product name on screen with nothing
 * behind it, which is worse than printing the number.
 *
 * So an unknown Model ID displays as six hex digits, which is true, and can
 * be looked up by whoever cares. To add an entry, resolve it against
 * Google's service and write down where the answer came from -- the same
 * standard the OUI tables in SpotterSignatures.h are held to. */
struct ModelName {
  uint32_t id;
  const char* name;
};
const ModelName kModels[] = {
  /* { 0x000000, "Example Buds" },  <- with a source, not from a spam list */
  { 0, nullptr },                   // sentinel; keeps the array non-empty
};
constexpr size_t kModelCount = sizeof(kModels) / sizeof(kModels[0]);

/* One length-and-type field. Advances *pos past it on success. */
bool readField(const uint8_t* data, size_t len, size_t* pos,
               uint8_t* typeOut, const uint8_t** bodyOut, uint8_t* bodyLen) {
  if (*pos >= len) {
    return false;
  }
  const uint8_t header = data[*pos];
  const uint8_t fieldLen = (uint8_t)(header >> 4);
  const uint8_t type = (uint8_t)(header & 0x0F);
  if (*pos + 1 + fieldLen > len) {
    return false;                     // runs off the end of the packet
  }
  *typeOut = type;
  *bodyOut = data + *pos + 1;
  *bodyLen = fieldLen;
  *pos += 1 + (size_t)fieldLen;
  return true;
}

void readBattery(const uint8_t* body, uint8_t bodyLen, bool hideUi,
                 Battery* out) {
  out->present = true;
  out->hideUi = hideUi;
  out->count = (bodyLen > 3) ? 3 : bodyLen;
  for (uint8_t i = 0; i < out->count; i++) {
    const uint8_t b = body[i];
    out->charging[i] = (b & 0x80) != 0;
    const uint8_t level = (uint8_t)(b & 0x7F);
    /* 0b1111111 is the spec's "I do not know". Anything above 100 is not a
     * percentage either, so it gets the same treatment rather than being
     * shown as a number that cannot be right. */
    out->level[i] = (level > 100) ? kLevelUnknown : level;
  }
}

}  // namespace

bool parse(const uint8_t* data, size_t len, Adv* out) {
  if (out == nullptr) {
    return false;
  }
  memset(out, 0, sizeof(*out));
  out->frame = Frame::None;
  out->battery.level[0] = kLevelUnknown;
  out->battery.level[1] = kLevelUnknown;
  out->battery.level[2] = kLevelUnknown;
  if (data == nullptr || len == 0) {
    return false;
  }

  /* The discoverable form is exactly three bytes and nothing else. The
   * not-discoverable form cannot be three bytes: it is a flags byte, then a
   * filter field whose header is one byte and whose smallest useful body is
   * four (the filter size is 1.2n + 3 bytes for n account keys), so the
   * shortest one carrying a filter is six. The only shorter shapes are the
   * empty ones, at one or two bytes.
   *
   * One gap, and it is the format's rather than this parser's: a flags byte
   * plus a single one-byte field is also three bytes, and no bit anywhere
   * distinguishes it from a Model ID. It does not occur -- the one-byte
   * fields are a salt, which is meaningless without the filter it salts,
   * and a one-component battery, which is only sent alongside one -- so the
   * length rule wins and a three-byte payload is always read as a Model ID.
   * tools/check_fastpair.py asserts this behaviour rather than wishing it
   * away. */
  if (len == 3) {
    out->frame = Frame::ModelId;
    out->modelId = ((uint32_t)data[0] << 16) |
                   ((uint32_t)data[1] << 8) |
                   (uint32_t)data[2];
    return true;
  }

  const uint8_t flagsByte = data[0];
  out->version = (uint8_t)(flagsByte >> 4);
  out->flags = (uint8_t)(flagsByte & 0x0F);

  /* Version 0 is the only one defined. A future version may reuse the
   * layout, but claiming to have parsed one would be a guess. */
  if (out->version != 0) {
    out->frame = Frame::None;
    return false;
  }

  if (len == 1) {
    out->frame = Frame::Empty;
    return true;
  }

  size_t pos = 1;
  bool sawFilter = false;

  while (pos < len) {
    uint8_t type = 0;
    const uint8_t* body = nullptr;
    uint8_t bodyLen = 0;
    if (!readField(data, len, &pos, &type, &body, &bodyLen)) {
      /* A field that overruns the packet means the rest cannot be trusted.
       * Keep what was read before it rather than discarding a frame that
       * was fine up to that point, but stop here. */
      break;
    }

    if (type == kTypeFilterShowUi || type == kTypeFilterHideUi) {
      sawFilter = true;
      out->filterHideUi = (type == kTypeFilterHideUi);
      uint8_t keep = bodyLen;
      if (keep > kMaxFilter) {
        keep = kMaxFilter;
        out->filterTruncated = true;
      }
      memcpy(out->filter, body, keep);
      out->filterLen = keep;
    } else if (type == kTypeSalt) {
      /* One or two bytes in practice. Anything else is not a salt shape we
       * can represent, so it is skipped rather than half-read. */
      if (bodyLen == 1) {
        out->haveSalt = true;
        out->salt = body[0];
      } else if (bodyLen == 2) {
        out->haveSalt = true;
        out->salt = (uint16_t)(((uint16_t)body[0] << 8) | body[1]);
      }
    } else if (type == kTypeBatteryShowUi || type == kTypeBatteryHideUi) {
      if (bodyLen > 0) {
        readBattery(body, bodyLen, type == kTypeBatteryHideUi, &out->battery);
      }
    }
    /* Unknown field types are skipped by length, which is what the
     * length-prefixed layout is for. */
  }

  /* A filter field with a zero-length body is the device saying it has no
   * account keys, which is the same statement as advertising no filter at
   * all. Both are Empty. */
  out->frame = (sawFilter && out->filterLen > 0) ? Frame::AccountKey
                                                 : Frame::Empty;
  return true;
}

const char* modelName(uint32_t modelId) {
  for (size_t i = 0; i < kModelCount; i++) {
    if (kModels[i].name != nullptr && kModels[i].id == modelId) {
      return kModels[i].name;
    }
  }
  return nullptr;
}

const char* frameText(Frame f) {
  switch (f) {
    case Frame::None:       return "not Fast Pair";
    case Frame::ModelId:    return "pairing mode";
    case Frame::AccountKey: return "paired";
    case Frame::Empty:      return "paired, no keys";
  }
  return "unknown";
}

}  // namespace FastPair
