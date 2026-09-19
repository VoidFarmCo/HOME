# Licensing

Pueo's own code is **GPL-3.0-or-later** as of this document. Upstream's MIT
notice is retained and those portions stay available under MIT.

The merged binary is now distributable under that licence too. It was not
when this document was first written: the tree carried a GPL-2.0-only
dependency that could not lawfully share a binary with two of the others, and
had done since 0.1.0. Removing it is recorded below rather than quietly
tidied away, because the reasoning is the useful part.

## What changed, and what did not

| | |
|---|---|
| `LICENSE` | the GNU GPL version 3 |
| `LICENSE.MIT` | upstream's notice, verbatim, Copyright (c) 2023 CiferTech |

Only the copyright holder can license their own work, so this covers the code
written for this fork. Everything inherited from
[ESP32-DIV](https://github.com/cifertech/ESP32-DIV) is CiferTech's and remains
MIT; anyone who wants those parts under MIT can take them from upstream, and
the notice travels with any redistribution of this tree. MIT is
GPL-compatible, so the combination is lawful. What it is not is *reversible* —
the combined work is GPL from here.

## The dependencies

Read off the installed sources rather than from memory or a README, because
the difference between "version 2" and "version 2 or later" is the whole
story and only the source says which.

| Library | Grant | Where it says so |
|---|---|---|
| ~~RF24 1.6.2~~ | ~~GPL-2.0-only~~ | **removed — see below** |
| **arduinoFFT** 1.6.2 | **GPL-3.0-or-later** | `arduinoFFT.h` |
| NimBLE-Arduino | Apache-2.0 | `LICENSE` |
| rc-switch | LGPL-2.1-or-later | `RCSwitch.h` |
| Adafruit_PN532 | BSD-3-Clause | `LICENSE` |
| Adafruit_BusIO, ArduinoJson, PCF8574 | MIT | `LICENSE` |
| XPT2046_Touchscreen | MIT | header comment |
| SmartRC-CC1101-Driver-Lib | MIT | `libs/.../VENDORED.md` |
| TFT_eSPI | BSD-family, no notice in the shipped zip | — |

RF24 was the problem, and the exact wording is why:

> RF24: "you can redistribute it and/or modify it under the terms of the GNU
> General Public License **version 2** as published by the Free Software
> Foundation."

No "or any later version". That is GPLv2-only. The "or later" that does
appear in RF24's `LICENSE` file is in the GPL's own *How to Apply These Terms*
appendix — boilerplate that ships with every copy of the licence text and
grants nothing. It is an easy thing to misread, and worth reading twice.

> arduinoFFT: "either version 3 of the License, or (at your option) any later
> version."

## The conflict, and how it was resolved

GPLv2-only and GPLv3 are incompatible. Each requires that the whole combined
work be distributable under its own terms, and neither permits the other. A
binary containing both cannot satisfy either. The firmware linked both:
`arduinoFFT` is instantiated in `wifi.cpp` and `subghz.cpp`, and RF24 was
used by the two jammers.

There was a second edge. Apache-2.0 is compatible with GPLv3 but **not** with
GPLv2, on account of its patent-termination clause, and NimBLE-Arduino is
Apache-2.0. So RF24 conflicted with NimBLE as well, and the set could not be
resolved by declaring the whole thing GPLv2 either.

RF24 was the odd one out in every direction; everything else in the tree is
permissive or GPLv3-compatible. **So it was removed.**

That turned out to be a much smaller job than it sounds, because almost
nothing used it. MouseJack, the ESB paths and the skimmer detector already
drove the chip's registers directly through two hand-rolled layers. RF24
survived only in the two jammers, and only for ten methods, nine of which are
a single register write each. `ESP32-DIV/Nrf24Raw.{h,cpp}` is now the one
owner of that register interface, the jammers use it, and the build no longer
installs the library.

The result is 3,476 bytes *smaller*, and it builds byte-identically with the
library deleted from the tree, which is how the removal was checked rather
than assumed.

With RF24 gone the remaining set is arduinoFFT (GPL-3.0-or-later),
NimBLE-Arduino (Apache-2.0), rc-switch (LGPL-2.1-or-later) and a handful of
MIT and BSD libraries. All of those are GPLv3-compatible, so the combined
work is distributable as GPL-3.0-or-later, which is what this fork is
licensed under.

## What that means in practice

**The source archive is fine**, and always was. It contains this fork's own
code and upstream's, both of which are ours to license, plus the vendored
CC1101 driver, which is MIT.

**The merged binary is fine as of the RF24 removal.** Distributing it means
distributing a combined work, and GPL-3.0-or-later now covers that
combination. The obligation that comes with it is the ordinary GPL one:
whoever receives the binary must be able to get the corresponding source.
This project publishes the archive alongside the image, which is what that
takes.

Images published **before** the removal — 0.1.0, 0.2.0 and 0.2.1 — contain
RF24 and are in the conflicted state described above. That is a licence
infringement against RF24's or arduinoFFT's authors, whoever chooses to mind,
and the tidy thing is to replace them with a build that is not.

## Ways out, and the one taken

**Replace RF24** — taken. It removed every edge at once, because nothing else
in the tree is GPLv2-only. It was expected to cost a full nRF24L01+ driver;
it cost one small file, because the features that do the interesting work
with that chip never used the library in the first place.

The others, recorded because they were real options and because they stay
relevant if a GPLv2-only dependency ever appears again:

**Drop `arduinoFFT`.** Used for exactly one thing in each of two features — a
Hamming window, a forward transform, a magnitude conversion. A small radix-2
FFT or any BSD/MIT one would remove the GPLv3 edge. It would have been
necessary but not sufficient, since RF24 also conflicted with NimBLE.

**Drop the NRF24 features.** Cheapest in effort, most expensive in product.

**Ship source only, no binary.** Sidesteps the question by not distributing
the combined work, at the cost of a 1 GB toolchain download for every user —
which is the reason the merged image exists at all.

## What the relicence did and did not buy

**Did:** made this fork's own code compatible with GPL-licensed work, which
is what the fingerprinting effort needs. `FlipDeFlock` is GPL-3.0-or-later,
so its signature data can now be used here with attribution — its name and
logo are separately restricted, see its `TRADEMARK.md`.

**Did not, on its own:** fix the binary. Relicensing made RF24 incompatible
with this project's own code as well as with two of its dependencies — it
added an edge to an already-broken graph. Removing RF24 is what repaired it.
Both steps were needed and only the second one was the fix.

## Redistribution gaps, while we are here

Two things this tree hands on without the notice they should carry:

- `Libraries/TFT_eSPI-master.zip` contains no root licence file, only one for
  the GFX fonts. `VENDORED.md` notes the same gap for the CC1101 driver.
- The source archive ships neither, since both are fetched or vendored rather
  than built from here. Anyone redistributing should add them.

## Not legal advice

This is a reading of licence texts in this repository by someone who is not a
lawyer. The facts in the table are checkable and worth checking. The
conclusions drawn from them are an opinion, and the conclusion that matters —
that a GPLv2-only library cannot be combined with GPLv3 — is the Free
Software Foundation's own published position, not a novel reading.
