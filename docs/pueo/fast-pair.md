# Google Fast Pair

The Apple side of this device was well covered — Continuity proximity
pairing, nearby action and offline finding are parsed in three separate
places. The Google side was not covered at all. The only `0xFE2C` that had
ever appeared in this tree was `GOOGLE_FAST_PAIR_ID`, a spoofing template
that nothing referenced, and it was deleted as dead code in the spoofer
work.

`ESP32-DIV/FastPair.{h,cpp}` parses the advertisement. It touches no BLE
stack, no display and no radio, which is what lets
`tools/check_fastpair.py` run it on a host.

Prompted by [FrostedFastPair](https://github.com/PivotChip/FrostedFastPair),
which is MIT and therefore fine to learn from here. None of its code was
copied; the payload handling below differs from it deliberately, for reasons
set out under "Two corrections".

## Where the payload lives

Fast Pair uses BLE **service data** under 16-bit UUID `0xFE2C`. Not
manufacturer data. That is why an Apple-shaped parser, which reaches for
`getManufacturerData()`, never sees a Fast Pair device at all — the three
Apple parsers in `bluetooth.cpp` are structurally incapable of noticing one.

`0xFEF3` is Nearby, a separate Google service that shares the neighbourhood
but not the format. It is named in `FastPair.h` so the distinction is on the
record; nothing parses it yet.

## Two shapes, one UUID

| | length | contents | meaning |
|---|---|---|---|
| **Discoverable** | exactly 3 | a 24-bit Model ID | in pairing mode, asking to be paired |
| **Not-discoverable** | 1, or 6+ | flags, account key filter, optional salt and battery | already paired to somebody |

The not-discoverable frame is a flags byte followed by length-and-type
fields: the upper nibble of each header byte is the body length, the lower
nibble is the type.

| type | field |
|---|---|
| `0x0` | account key filter, show UI indication |
| `0x1` | salt |
| `0x2` | account key filter, hide UI indication |
| `0x3` | battery, show UI |
| `0x4` | battery, hide UI |

Battery bytes are one per component — left bud, right bud, case. Bit 7 is
charging, the low seven bits are a percentage, and `0b1111111` means
unknown. Anything else above 100 is also reported unknown rather than
printed as a percentage that cannot be right.

Unknown field types are stepped over by their length, which is what the
length prefix is for.

### The one ambiguity

A flags byte plus a single one-byte field is also three bytes, and no bit
anywhere distinguishes that from a Model ID. The length rule wins: three
bytes is always read as a Model ID.

It does not arise in practice. The only one-byte fields are a salt, which is
meaningless without the account key filter it salts, and a one-component
battery, which is only sent alongside a filter. Both real shapes carry a
filter and are therefore six bytes or longer. It is asserted in the checker
rather than wished away.

## Two corrections

These are the reasons the parser here is not a transcription of the one that
prompted it.

**Most devices are not in pairing mode, and the naive read invents a Model
ID for them.** `modelId = data[0]<<16 | data[1]<<8 | data[2]` is correct for
a discoverable frame and produces a confident, plausible, entirely
meaningless six-hex-digit number for every other frame. Since most earbuds
in the air already belong to somebody, that is the common case rather than
the edge case. The failure is silent — nothing about the output looks wrong.
`check_fastpair.py` pins it: for the frame `00 40 01 02 03 04 11 C7`, the
naive read yields `004001`, and the parser here reports no Model ID at all.

**Fast Pair gives a passive listener no stable per-unit identifier, so
nothing should correlate on one.**

- A **Model ID identifies a model**. `2B71B2` means "one of these", not
  "this one". Two strangers with the same earbuds advertise the same value.
  Using it to hold identity across MAC rotation merges their devices into
  one row.
- An **account key filter is a salted bloom filter** and the salt rotates.
  That is its entire purpose: it exists so a passive listener *cannot*
  follow the device. The same device under a new salt looks like a different
  device, which is the design working.
- A **device name** is not an identifier either, for the reasons already
  written down in `SpotterSignatures.h`.

So a Fast Pair device that implements the spec correctly cannot be tracked
passively across address rotation. Any tool that claims to is matching on a
model or a name. That is worth saying plainly because the temptation is
real, the code to do it is four lines, and it produces a device list that
looks better and is wrong.

This is the same merge hazard `check_spotter_merge.py` exists to catch, and
it is why the Fast Pair recogniser does not feed Spotter's `findOrAdd`.

## The model name table is empty

`kModels` in `FastPair.cpp` has no entries, and that is a decision.

Google publishes the mapping through the Nearby Devices metadata service,
but it needs an API key, so nothing here can verify an entry at build time
or at read time. The Model ID lists that circulate for ESP32 and Flipper
"Fast Pair spam" tools are lists of IDs that *provoke a pairing popup*; the
product names printed beside them are frequently wrong, because provoking a
popup does not require the name to be right. Copying one in would put a
confident product name on screen with nothing behind it.

An unrecognised Model ID displays as six hex digits, which is true and can
be looked up. To add an entry, resolve it against Google's service and
record where the answer came from — the standard the OUI tables in
`SpotterSignatures.h` are held to.

## What is checked

`tools/check_fastpair.py` transcribes `parse()` into Python and runs 80,111
checks: both frame shapes, every field type, battery encoding including the
unknown and out-of-range levels, field headers that overrun the packet, a
good field followed by a bad one, unknown field types, version refusal,
80,000 fuzz inputs, and every single-byte mutation of a real frame against
six replacement values.

Two of them are about what the parser must *not* promise: that two units of
the same model are indistinguishable, and that one device under two salts
looks like two devices. They are there so that anybody who later adds
correlation has to delete a test that says why not.

Every fuzz input additionally asserts `(frame == ModelId) == (len == 3)`,
which is the whole rule in one line.
