# Reading Flipper `.sub` files

Pueo's sub-GHz captures live in a packed struct in EEPROM and export as a
binary blob with a magic number, which interoperates with exactly nothing.
`.sub` is what the rest of the sub-GHz world trades in. Being able to read
one is the difference between a capture that travels and a capture that
stops at this device.

`ESP32-DIV/SubFile.{h,cpp}` parses the text. It touches no SD card, no
display and no radio, which is what lets `tools/check_sub_parse.py` run it on
a host.

## The format

A key file, which is the case worth handling:

```
Filetype: Flipper SubGhz Key File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: Princeton
Bit: 24
Key: 00 00 00 00 00 12 34 56
TE: 403
```

Key-value text, one field per line, split on the first colon. `Key` is eight
space-separated hex bytes, big-endian, zero-padded on the left. Unknown
lines are ignored rather than refused, because Flipper firmwares add fields.
CRLF is accepted and the file need not end in a newline.

## RAW is refused, on purpose

A RAW file has the same `Filetype` prefix, the same `Frequency` and the same
`Preset`, and then carries microsecond timings where the key would be:

```
Filetype: Flipper SubGhz RAW File
Protocol: RAW
RAW_Data: 331 -179 337 -181 332 -180
```

A `SubGhzProfile` holds a value and a bit count. There is nowhere for
timings to go. Parsing one anyway would produce a profile that looks valid
in the list and transmits nonsense, which is worse than a file that will not
open, so RAW is refused by name, and by three separate signals: the
`Filetype`, a `Protocol: RAW` line, and the presence of `RAW_Data`. Any one
of them is enough.

Supporting RAW properly means storing timing arrays and driving the CC1101
from them. That is a different feature, not a bigger parser.

## Protocol names, and the one mapping that is claimed

`SubGhzProfile::protocol` is an rc-switch protocol number. Flipper's
`Protocol` field is a name from a different library with different timings.
The two do not line up, so only one mapping is made:

| Flipper name | rc-switch | why |
|---|---|---|
| `Princeton` | 1 | PT2262/EV1527, and rc-switch protocol 1 is the same thing |
| everything else | **0** | the name is kept in `protocolName`; no number is guessed |

CAME, NICE FLO, Holtek, Linear, KeeLoq and the rest parse fine and come back
with `protocol = 0`, meaning "here is the name, decide for yourself". A
guessed number would transmit something subtly wrong while presenting as
correct, which is the failure this project keeps declining to build.

Even Princeton is approximate. rc-switch protocol 1 assumes a 350 µs pulse
and the file carries its own `TE`, usually nearer 400. `TE` is parsed and
kept for whoever wants to act on it; nothing acts on it yet.

## What is checked

A `.sub` arrives from somebody else's SD card, so it is untrusted input in
the same sense a probe request is.

- `Frequency` must be 280–960 MHz. Outside that is `FreqOutOfRange` rather
  than tuned to.
- `Bit` must be 1–32. `SubGhzProfile::value` is a `uint32_t`; anything
  larger is `TooManyBits`, not a silent truncation.
- `Key` must be whole hex byte pairs, at most eight, each followed by a
  separator or the end. Anything else is `BadField`.
- `Frequency` and `Bit` are parsed with an overflow check, so a 40-digit
  number is refused rather than wrapped.
- Every string field is copied into a fixed buffer, trimmed and terminated.

`tools/check_sub_parse.py` transcribes `parse()` into Python and runs 60,891
checks: the real-world cases above, RAW three ways, each required field
dropped in turn, the range edges, 60,000 fuzz inputs, and every single-byte
mutation of a valid file against five replacement characters. Every input
must produce one of the seven results and nothing else.

## Not wired up yet

Nothing calls `SubFile::parse()`. The build size is unchanged because the
linker garbage-collects it.

Wiring it in means more than calling the parser. `saveProfile()`
(`ESP32-DIV/subghz.cpp:1105`) is UI-bound (it reads globals and drives the
screen rather than taking a profile as an argument), and
`importProfilesFromSD()` handles only Pueo's own binary format, header magic
and all. There are also only `MAX_PROFILES = 5` slots in EEPROM, so an
import has to ask which one it is replacing.

That is a UI change, and it is kept separate from the parser deliberately,
the same way EAPOL went: the part that can be tested on a host lands first
and is proved, then the part that can only be judged on hardware.

## Writing `.sub`

Not done. Export is the more useful direction of the two (a Pueo capture
that opens on a Flipper), and it is easier, because it is formatting rather
than parsing. It needs a `Preset` name chosen for the CC1101 settings
actually used, and a decision about what to write in `Protocol` for a
capture whose rc-switch number has no Flipper equivalent, which is the same
mapping problem from the other side.
