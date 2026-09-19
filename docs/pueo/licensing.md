# Licensing

Pueo's own code is **GPL-3.0-or-later** as of this document. Upstream's MIT
notice is retained and those portions stay available under MIT.

That is the easy half. The hard half is that **the merged binary cannot be
distributed under any single licence as it stands**, and that has been true
since 0.1.0. It is not caused by the relicence; the relicence is what made it
worth looking at properly.

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
| **RF24** 1.6.2 | **GPL-2.0-only** | `RF24.h`, `RF24.cpp`, `RF24_config.h` |
| **arduinoFFT** 1.6.2 | **GPL-3.0-or-later** | `arduinoFFT.h` |
| NimBLE-Arduino | Apache-2.0 | `LICENSE` |
| rc-switch | LGPL-2.1-or-later | `RCSwitch.h` |
| Adafruit_PN532 | BSD-3-Clause | `LICENSE` |
| Adafruit_BusIO, ArduinoJson, PCF8574 | MIT | `LICENSE` |
| XPT2046_Touchscreen | MIT | header comment |
| SmartRC-CC1101-Driver-Lib | MIT | `libs/.../VENDORED.md` |
| TFT_eSPI | BSD-family, no notice in the shipped zip | — |

The two in bold are the problem, and the exact wording is why:

> RF24: "you can redistribute it and/or modify it under the terms of the GNU
> General Public License **version 2** as published by the Free Software
> Foundation."

No "or any later version". That is GPLv2-only. The "or later" that does
appear in RF24's `LICENSE` file is in the GPL's own *How to Apply These Terms*
appendix — boilerplate that ships with every copy of the licence text and
grants nothing. It is an easy thing to misread, and worth reading twice.

> arduinoFFT: "either version 3 of the License, or (at your option) any later
> version."

## The conflict

GPLv2-only and GPLv3 are incompatible. Each requires that the whole combined
work be distributable under its own terms, and neither permits the other's.
A binary containing both cannot satisfy either.

The firmware links both. `arduinoFFT` is instantiated in `wifi.cpp:351` and
`subghz.cpp:484`; RF24 is throughout `bluetooth.cpp`, `utils.cpp` and
`SpiBus.cpp`. So the merged image combines them, and has since 0.1.0.

There is a second edge. Apache-2.0 is compatible with GPLv3 but **not** with
GPLv2, on account of its patent-termination clause. NimBLE-Arduino is
Apache-2.0 and is linked. So RF24 conflicts with NimBLE as well, which means
the set cannot be resolved by simply declaring the whole thing GPLv2 either.

RF24 is the odd one out in every direction. Everything else in the tree is
either permissive or GPLv3-compatible.

## What that means in practice

**The source archive is fine.** It contains this fork's own code and
upstream's, both of which are ours to license, plus the vendored CC1101
driver, which is MIT. Nothing in it is in conflict.

**The merged binary is the problem.** Distributing it means distributing a
combined work, and no licence covers that combination. Publishing it with the
full source alongside — which this project does — addresses the *spirit* of
GPL's source requirement but does not resolve an incompatibility between two
dependencies. No amount of disclosure makes GPLv2-only and GPLv3 compatible.

This is not a theoretical wrinkle in one respect: it is a licence
infringement against RF24's or arduinoFFT's authors, whoever chooses to mind.
It is theoretical in another: both are hobby libraries, the project is
non-commercial, and nobody has complained. Those are different questions and
only the first one is a matter of fact.

## Ways out

Ordered by how little they cost.

**Drop `arduinoFFT`.** It is used for exactly one thing in each of two
features — a Hamming window, a forward transform and a magnitude conversion,
three calls apiece. A small self-written radix-2 FFT, or any BSD/MIT one,
removes the GPLv3 edge. That leaves RF24 GPLv2-only against NimBLE's
Apache-2.0, so it is necessary but not sufficient.

**Replace RF24.** This is the real fix, and it removes every edge at once,
because nothing else in the tree is GPLv2-only. The cost is a driver for the
nRF24L01+ that the MouseJack, ESB replay and jammer features are written
against. That is not a weekend, but the nRF24 is a simple part and the
register interface is documented.

**Drop the NRF24 features.** Cheapest in effort, most expensive in product:
it is a third of what the thing does.

**Ship source only, no binary.** Sidesteps the question by not distributing
the combined work. Costs every user a 1 GB toolchain download, which is the
reason the merged image exists in the first place.

Doing nothing is also a position, as long as it is a chosen one rather than
an assumed one. That is what this document is for.

## What the relicence did and did not buy

**Did:** made this fork's own code compatible with GPL-licensed work, which
is what the fingerprinting effort needs. `FlipDeFlock` is GPL-3.0-or-later,
so its signature data can now be used here with attribution — its name and
logo are separately restricted, see its `TRADEMARK.md`.

**Did not:** fix the binary. RF24 is now incompatible with this project's own
code as well as with two of its dependencies. The graph was already broken;
this adds an edge to it rather than repairing one.

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
