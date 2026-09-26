# Contributing to Pueo

Pueo is a fork of [ESP32-DIV](https://github.com/cifertech/ESP32-DIV) by
CiferTech, targeting a stock "cheap yellow display" with modules bolted on.
This file used to be upstream's, unedited, which meant it sent people to
their repository and their branches. It is Pueo's now.

If you want to contribute to ESP32-DIV itself, go there instead. Fixes that
are not CYD-specific are often better off upstream, and several from this
fork have gone that way.

---

## Before you write anything

Say what you are doing first, in
[an issue or a discussion](README.md#issues-or-discussions). Not ceremony:
this tree has a lot of load-bearing detail that is not obvious from reading
it, and half an hour of conversation can save a rewrite.

Two kinds of report are worth more than code here, because nobody working
on it can produce them:

- **Anything from the 2.8&Prime; ESP32-2432S028R.** It is built every release
  and has never been booted.
- **A near miss**, such as the 3248S035**C** with capacitive touch. GPIO 25
  goes to its GT911 and this image drives a chip select into that pin.

---

## Building

```bash
bash tools/build.sh
```

That is the build. There is no Arduino IDE workflow to follow, no
`platform.txt` to swap by hand, and no libraries to copy into place;
`build.sh` does all of it and strips upstream's `-w` and `-zmuldefs` so the
compiler is allowed to talk. See
[docs/pueo/warnings.md](docs/pueo/warnings.md) and
[zmuldefs.md](docs/pueo/zmuldefs.md) for what those two were hiding, one of
which turned out to be load-bearing.

**Arduino core 2.0.10 exactly. Do not bump it.** Newer cores change NimBLE
and the WiFi driver in ways this tree depends on not changing.

Two builds exist, selected by environment:

| | |
|---|---|
| `PUEO_PANEL_35=1` | 3.5&Prime; ESP32-3248S035R, the reference board |
| `PUEO_PANEL_35=0` | 2.8&Prime; ESP32-2432S028R |
| `PUEO_ROLE=detector` | the firmware |
| `PUEO_ROLE=beacon` | PueoBeacon, the bench transmitter |

---

## What has to pass

```bash
for t in tools/check_*.py; do python "$t" || echo "FAIL $t"; done
```

All of them, before a pull request. They are not lint. Each one exists
because something specific went wrong and shipped:

- `check_menu_dispatch.py` and `check_menu_tables.py` exist because a menu
  rebalance renumbered branches without their bodies, and ARP exited into
  Hidden SSID for a release.
- `check_nrf24_rpd.py` exists because the RPD latches, so a sweep that
  forgets to drop CE reports the whole band busy and looks plausible doing
  it.
- `check_pinmap.py` runs inside the build, because two features claiming
  one pin is the failure this hardware makes easiest.

**If you fix a bug that a check would have caught, add the check.** Then
break the fix on purpose and confirm the check fails. A check that has
never failed has not been tested.

The build must stay `-Wall -Wextra` clean.

---

## Licensing, which is stricter here than upstream

Upstream is MIT. **This fork is GPL-3.0-or-later**, and that constrains what
can be added.

Release 0.2.2 removed RF24 because it is **GPL-2.0-only**, which cannot
lawfully share a binary with arduinoFFT or NimBLE-Arduino. The conflict was
inherited and sat there from 0.1.0. 0.2.1 is pinned on the download page
forever as the last release that contained it.

So, for any new dependency:

- MIT, BSD, Apache-2.0 and GPL-3.0-or-later are fine.
- **GPL-2.0-only is not**, no matter how convenient the library is.
- If you need what a GPL-2.0-only library does, drive the registers. That is
  what [Nrf24Raw](ESP32-DIV/Nrf24Raw.h) is, and the interface to that part
  turned out to be twenty lines.

See [docs/pueo/licensing.md](docs/pueo/licensing.md).

---

## Reproducible builds

Every release's `merged.bin` rebuilds byte-identically from its own
`-src.zip`. That is a published property, checked before each release, and
it is worth not breaking.

It means no timestamps, no build counters, and no absolute paths in the
image. `build.sh` passes `-ffile-prefix-map` for the last one. Extract to a
short path such as `C:\pv` when verifying: NimBLE's sources sit deep enough
that a temp directory blows past Windows' path limit, and a junction does
not help because the compiler canonicalises through it.

---

## Code

Follow what is there. The patterns that matter:

- **Namespace per feature**, exposing `Setup()` and `Loop()`.
- Exit a feature with `feature_exit_requested = true`, not `return` or
  `break` from inside its loop.
- **Pin assignments belong in [board_pueo.h](ESP32-DIV/board_pueo.h)**, not
  in feature files and not in `shared.h`. Pins that differ between the two
  panels are `#if PUEO_PANEL_35`, and the header says why for each one.
- No blocking `delay()` over about 200 ms in a feature loop; use `millis()`.
- Display writes go through the theme constants in `shared.h`.
- `#pragma once` in new headers.

**Comments should say why, not what.** This tree is unusual in how much it
explains, and that is deliberate: most of the hard-won facts in it are
things that look like mistakes until you know the reason. If you move a
number, leave the reasoning where the number was.

---

## Pull requests

- One thing per pull request.
- Target the **`pueo`** branch.
- Say **which panel you tested on**, and say so plainly if you only
  compiled. Compile-only is acceptable for the 2.8&Prime;, since nobody has
  a booted one, but it has to be stated rather than implied.
- Include the check output if you added or changed one.

Commit messages: a short subject line in the imperative, then prose
explaining why. Look at `git log` before writing one. The convention here is
that the message carries the reasoning that does not fit in a comment.

---

## Code of conduct

Be respectful and constructive. Harassment, discrimination, or abuse of any
kind is not welcome here.

---

## A word about what this is for

Pueo transmits, and several of its features jam or impersonate. It is for
networks and devices you own or have written permission to test.
Contributions that only make sense for use against other people's equipment
are not wanted, and neither is help with that.
