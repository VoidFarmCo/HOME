#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * SubFile — reading Flipper Zero `.sub` sub-GHz captures.
 *
 * Pueo stores captures in its own packed struct and exports them as a binary
 * blob, which interoperates with nothing. `.sub` is the format the rest of
 * the sub-GHz world trades in, so being able to read one is the difference
 * between a capture that travels and a capture that does not.
 *
 * A key file looks like this, and is the case worth handling:
 *
 *     Filetype: Flipper SubGhz Key File
 *     Version: 1
 *     Frequency: 433920000
 *     Preset: FuriHalSubGhzPresetOok650Async
 *     Protocol: Princeton
 *     Bit: 24
 *     Key: 00 00 00 00 00 12 34 56
 *     TE: 403
 *
 * A RAW file carries microsecond timings instead of a key. Those cannot
 * become a SubGhzProfile, which holds a value and a bit count, so they are
 * refused by name rather than mangled into something that looks valid.
 *
 * This parses text and nothing else: no SD, no display, no radio. That is
 * what lets tools/check_sub_parse.py run it on a host, and it is a file from
 * somebody else's card, so every field is bounded before it is read.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include <stdint.h>

namespace SubFile {

enum class Result : uint8_t {
  Ok = 0,
  NotSubFile,      // no Filetype line, or not a Flipper SubGhz one
  RawUnsupported,  // timings rather than a key
  MissingField,    // no Frequency, Bit or Key
  BadField,        // a field that will not parse
  TooManyBits,     // Bit above 32, which will not fit the value
  FreqOutOfRange,  // not a frequency this hardware can reach
};

struct Parsed {
  uint32_t frequency;      // Hz
  uint32_t value;          // the low `bitLength` bits of Key
  uint16_t bitLength;
  uint16_t protocol;       // rc-switch number, or 0 when not recognised
  uint16_t te;             // microseconds, 0 when absent
  char     protocolName[24];
  char     preset[40];
};

/**
 * Parse `len` bytes of `.sub` text into `out`.
 *
 * `out` is only meaningful when the result is Ok. Both CRLF and LF line
 * endings are accepted, and a file need not end in a newline.
 */
Result parse(const char* text, size_t len, Parsed* out);

/** A short phrase for a result, for putting on screen. */
const char* resultText(Result r);

}  // namespace SubFile
