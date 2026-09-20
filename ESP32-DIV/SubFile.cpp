#include "SubFile.h"

#include <string.h>

namespace SubFile {

namespace {

/* The band this hardware can actually reach. A file naming 868 MHz on a
 * 433-only module is better refused than tuned to. */
constexpr uint32_t kFreqMinHz = 280000000UL;
constexpr uint32_t kFreqMaxHz = 960000000UL;

/* Protocol names, mapped to rc-switch numbers, and only where the mapping is
 * defensible.
 *
 * Princeton is PT2262/EV1527, which is rc-switch protocol 1, and that one is
 * safe. The rest of Flipper's names -- CAME, NICE FLO, Holtek, Linear and so
 * on -- have timings that do not line up with an rc-switch protocol number,
 * and guessing would produce a profile that transmits something subtly wrong.
 * They parse fine and come back with protocol 0, which means "the name is in
 * protocolName, decide for yourself".
 *
 * Note that even Princeton is approximate: rc-switch protocol 1 assumes a
 * 350 us pulse and the file carries its own TE, usually around 400. TE is
 * preserved for whoever wants to act on it. */
struct ProtoMap {
  const char* name;
  uint16_t rcSwitch;
};
const ProtoMap kProtocols[] = {
  {"Princeton", 1},
};
constexpr size_t kProtocolCount = sizeof(kProtocols) / sizeof(kProtocols[0]);

bool isSpace(char c) {
  return c == ' ' || c == '\t' || c == '\r';
}

/* Copy src into dst, trimmed, truncated, always terminated. */
void copyTrimmed(char* dst, size_t dstSz, const char* src, size_t srcLen) {
  if (dstSz == 0) {
    return;
  }
  size_t begin = 0;
  while (begin < srcLen && isSpace(src[begin])) {
    begin++;
  }
  size_t end = srcLen;
  while (end > begin && isSpace(src[end - 1])) {
    end--;
  }
  size_t n = end - begin;
  if (n > dstSz - 1) {
    n = dstSz - 1;
  }
  memcpy(dst, src + begin, n);
  dst[n] = '\0';
}

/* Decimal, no sign. False on empty or on any non-digit. */
bool parseU32(const char* s, size_t len, uint32_t* out) {
  size_t i = 0;
  while (i < len && isSpace(s[i])) {
    i++;
  }
  size_t end = len;
  while (end > i && isSpace(s[end - 1])) {
    end--;
  }
  if (i >= end) {
    return false;
  }
  uint32_t acc = 0;
  for (; i < end; i++) {
    if (s[i] < '0' || s[i] > '9') {
      return false;
    }
    const uint32_t digit = (uint32_t)(s[i] - '0');
    if (acc > (0xFFFFFFFFUL - digit) / 10UL) {
      return false;                     // would overflow
    }
    acc = acc * 10UL + digit;
  }
  *out = acc;
  return true;
}

int hexVal(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  return -1;
}

/* "00 00 00 00 00 12 34 56" -> 0x0000000000123456, big endian, any number of
 * space-separated byte pairs. The low 32 bits are what a profile can hold, so
 * the caller checks bitLength before trusting them. */
bool parseKey(const char* s, size_t len, uint64_t* out, int* bytesOut) {
  uint64_t acc = 0;
  int bytes = 0;
  size_t i = 0;
  while (i < len) {
    while (i < len && isSpace(s[i])) {
      i++;
    }
    if (i >= len) {
      break;
    }
    const int hi = hexVal(s[i]);
    if (hi < 0 || i + 1 >= len) {
      return false;
    }
    const int lo = hexVal(s[i + 1]);
    if (lo < 0) {
      return false;
    }
    if (bytes >= 8) {
      return false;                     // more than 64 bits of key
    }
    acc = (acc << 8) | (uint64_t)((hi << 4) | lo);
    bytes++;
    i += 2;
    /* A byte must be followed by a separator or the end. */
    if (i < len && !isSpace(s[i])) {
      return false;
    }
  }
  if (bytes == 0) {
    return false;
  }
  *out = acc;
  *bytesOut = bytes;
  return true;
}

bool containsCI(const char* hay, const char* needle) {
  const size_t hn = strlen(hay);
  const size_t nn = strlen(needle);
  if (nn == 0 || nn > hn) {
    return false;
  }
  for (size_t i = 0; i + nn <= hn; i++) {
    size_t j = 0;
    while (j < nn) {
      char a = hay[i + j];
      char b = needle[j];
      if (a >= 'A' && a <= 'Z') a = (char)(a - 'A' + 'a');
      if (b >= 'A' && b <= 'Z') b = (char)(b - 'A' + 'a');
      if (a != b) break;
      j++;
    }
    if (j == nn) {
      return true;
    }
  }
  return false;
}

}  // namespace

Result parse(const char* text, size_t len, Parsed* out) {
  if (text == nullptr || out == nullptr) {
    return Result::NotSubFile;
  }
  memset(out, 0, sizeof(*out));

  char filetype[48] = {0};
  bool haveFiletype = false;
  bool haveFreq = false;
  bool haveBit = false;
  bool haveKey = false;
  bool sawRawData = false;

  uint32_t freq = 0;
  uint32_t bits = 0;
  uint64_t key = 0;
  int keyBytes = 0;

  size_t pos = 0;
  while (pos < len) {
    size_t eol = pos;
    while (eol < len && text[eol] != '\n') {
      eol++;
    }
    const char* line = text + pos;
    const size_t lineLen = eol - pos;
    pos = (eol < len) ? eol + 1 : len;

    /* Split on the first colon. A line without one is not a field. */
    size_t colon = 0;
    while (colon < lineLen && line[colon] != ':') {
      colon++;
    }
    if (colon >= lineLen) {
      continue;
    }

    char keyName[24];
    copyTrimmed(keyName, sizeof(keyName), line, colon);
    const char* val = line + colon + 1;
    const size_t valLen = lineLen - colon - 1;

    if (strcmp(keyName, "Filetype") == 0) {
      copyTrimmed(filetype, sizeof(filetype), val, valLen);
      haveFiletype = true;
    } else if (strcmp(keyName, "Frequency") == 0) {
      if (!parseU32(val, valLen, &freq)) {
        return Result::BadField;
      }
      haveFreq = true;
    } else if (strcmp(keyName, "Bit") == 0) {
      if (!parseU32(val, valLen, &bits)) {
        return Result::BadField;
      }
      haveBit = true;
    } else if (strcmp(keyName, "Key") == 0) {
      if (!parseKey(val, valLen, &key, &keyBytes)) {
        return Result::BadField;
      }
      haveKey = true;
    } else if (strcmp(keyName, "TE") == 0) {
      uint32_t te = 0;
      if (parseU32(val, valLen, &te) && te <= 0xFFFFUL) {
        out->te = (uint16_t)te;
      }
    } else if (strcmp(keyName, "Protocol") == 0) {
      copyTrimmed(out->protocolName, sizeof(out->protocolName), val, valLen);
    } else if (strcmp(keyName, "Preset") == 0) {
      copyTrimmed(out->preset, sizeof(out->preset), val, valLen);
    } else if (strcmp(keyName, "RAW_Data") == 0) {
      sawRawData = true;
    }
  }

  if (!haveFiletype || !containsCI(filetype, "Flipper SubGhz")) {
    return Result::NotSubFile;
  }

  /* RAW is refused by name rather than half-parsed: it has timings where a
   * key would be, and nothing here can carry those. */
  if (sawRawData || containsCI(filetype, "RAW") ||
      containsCI(out->protocolName, "RAW")) {
    return Result::RawUnsupported;
  }

  if (!haveFreq || !haveBit || !haveKey) {
    return Result::MissingField;
  }
  if (freq < kFreqMinHz || freq > kFreqMaxHz) {
    return Result::FreqOutOfRange;
  }
  if (bits == 0 || bits > 32) {
    return Result::TooManyBits;
  }

  out->frequency = freq;
  out->bitLength = (uint16_t)bits;
  out->value = (uint32_t)(key & 0xFFFFFFFFULL);

  for (size_t i = 0; i < kProtocolCount; i++) {
    if (strcmp(out->protocolName, kProtocols[i].name) == 0) {
      out->protocol = kProtocols[i].rcSwitch;
      break;
    }
  }
  return Result::Ok;
}

const char* resultText(Result r) {
  switch (r) {
    case Result::Ok:             return "ok";
    case Result::NotSubFile:     return "not a .sub file";
    case Result::RawUnsupported: return "RAW capture, no key";
    case Result::MissingField:   return "missing Frequency/Bit/Key";
    case Result::BadField:       return "unreadable field";
    case Result::TooManyBits:    return "bit count out of range";
    case Result::FreqOutOfRange: return "frequency out of range";
  }
  return "unknown";
}

}  // namespace SubFile
