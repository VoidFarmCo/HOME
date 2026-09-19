# Probe-request IE fingerprinting

Scope for a Spotter detection path that survives MAC randomisation, and the
reason its signature table has to start empty.

Steps 1 to 3 of the phasing at the end are built: the walk, the hash, the
fingerprint on screen, and the three things that work without any signature
table at all. Nothing matches against a signature yet, and nothing can until
there are captures to match against.

## Why

Spotter identifies a device three ways: the OUI of its transmitter address,
the SSID it probes for, and what it advertises over BLE. Two of those are
strong. The OUI is the weak one, and it is weak in a way that is getting
worse rather than better.

The 33 community prefixes now in `SpotterSignatures.h` resolve mostly to
Liteon and Espressif, which is why they are all `Weak`. But there is a
sharper problem underneath that, and it is already visible in the data:
`82:6B:F2` has the locally-administered bit set. It is not a vendor block at
all. `flock-finder` reports newer units using locally-administered addresses
specifically to defeat prefix matching, and a randomised address is
randomised -- keeping a list of them is keeping a list of dice rolls.

A camera that randomises its MAC defeats the OUI table completely, and it
also defeats the `Hit` table itself: `findOrAdd` keys on the MAC, so one
camera rotating addresses occupies as many of the 48 rows as it likes.

What does not change when the address does is the radio. A probe request
carries a set of information elements whose identity, order and contents are
decided by the chipset, the driver and the supplicant. Two units of the same
model produce the same set. That is the thing worth matching on.

## What the fingerprint is

A probe request is a fixed 24-byte header followed by tagged elements, each
`id`, `len`, `value`. A typical one carries some of:

| id | element | in the hash |
|---|---|---|
| `0x00` | SSID | id only |
| `0x01` | Supported Rates | id and contents |
| `0x03` | DS Parameter Set | id only |
| `0x2D` | HT Capabilities | id and contents |
| `0x32` | Extended Supported Rates | id and contents |
| `0x7F` | Extended Capabilities | id and contents |
| `0xBF` | VHT Capabilities | id and contents |
| `0xDD` | Vendor Specific | id, OUI and vendor type |
| `0xFF` | Element Extension | id and extension id |
| anything else | | id only |

The fingerprint is a hash over that sequence, in the order the elements
actually appear, because order is itself part of the signature.

Every element contributes its id, including the two below: that an element
is present at all, and where it sits in the order, is decided by the driver
and is signal on its own. What the two below do not contribute is contents,
and those two exclusions matter more than any of the inclusions.

**The SSID's contents must not be in the hash.** It is the one field that
varies between two probes from the same device, and it is already matched
separately. Including it would produce a fingerprint per network name rather
than per device -- and a wildcard probe would not match a directed one from
the same radio.

**The DS Parameter Set's contents must not be in the hash.** It carries the
current channel, and Spotter hops channels every 260 ms. Including it would
give the same camera up to thirteen different fingerprints.

(The first draft of this document excluded both elements outright, ids and
all. That is worse: whether a device emits a DS Parameter Set in a probe at
all is stable per driver, and throwing the id away throws that away with the
channel. `tools/fuzz_ie_walk.py` holds the distinction to account -- it
asserts that changing the SSID or the channel leaves the fingerprint alone
while changing the supported rates moves it.)

For vendor-specific elements only the 3-byte OUI and the vendor type go in,
not the payload. The payload of a WPS or Apple element carries device state
that changes between probes; the fact that the element is present, and whose
it is, does not.

FNV-1a over that canonical byte sequence gives a `uint32_t`. The choice of
hash is not interesting -- it needs to be cheap and well distributed, not
cryptographic.

## Where it lands

`onPacket` in `Spotter.cpp`, which had to be restructured first.

It used to read the SSID straight off the front of the tagged region and
return if the first element was not one, or if it was zero length:

```c
if (ssidLen == 0 || ssidLen > 32 || len < (uint16_t)(ieStart + 2 + ssidLen)) {
  return;
}
```

That was correct for what it did. It was also fatal for fingerprinting,
because a zero-length SSID is a **wildcard probe request** -- a device asking
"is anyone there" rather than "is *my* network there" -- and those are a
large fraction of the probes in the air. They carry the full element set.
Dropping them threw away most of the evidence.

So the walk comes first and the SSID match is one case inside it:

1. Walk the elements once, bounds-checked.
2. Accumulate the hash as described.
3. If element `0x00` is present and non-empty, run the SSID match.
4. Annotate the row, if a signature created one, with the hash.
5. (Step 5 of the phasing) match the hash against the fingerprint table.

One pass, no second traversal. The annotation is last so that a row created
by either the OUI or the SSID match gets its fingerprint in the same frame
that created it.

## Cost

`onPacket` runs on the WiFi task, not in an interrupt, so calling into
flash-resident code is fine. The walk is one pass over the tagged region --
a probe request is rarely more than 150 bytes of elements -- and the hash is
an xor and a multiply per byte fed. Against the 36 three-byte `memcmp`s the
OUI table already does per frame, this is not the expensive part.

Matching is one `uint32_t` compare per table entry, which is cheaper per
signature than anything Spotter does now.

Memory, against 275,579 bytes of flash and 219,276 of RAM free at 0.2.1:

| | |
|---|---|
| `Hit` grows by a `uint32_t` fingerprint | 48 x 4 = 192 B RAM |
| fingerprint-group table, 16 entries | ~192 B RAM |
| walker, hash, matcher | 1-2 KB flash |
| signature table | 12 B per entry |

Nothing here is close to a constraint.

## The parsing is hostile input

This walks an attacker-shaped buffer. Every step checks `id`, `len` and
`value` against `sig_len` before reading, the arithmetic is `uint32_t` so a
large offset plus a large claimed length cannot wrap back into range, and
there is a hard iteration cap on top of the bound. `len` is a single byte
from the air and `ieStart + 2 + len` can run past the end of a truncated
frame.

Step 2 widened the reach considerably: the walk no longer reads only ids and
lengths but element contents, up to every byte of a capabilities element.
`tools/fuzz_ie_walk.py` transcribes the walk into Python behind a buffer
that refuses any read outside the frame and runs 65,543 frames through it,
including an element of every contents-reading id claiming more than the
frame holds, and elements ending exactly on the last byte. It says nothing
about the compiled C -- there is no host compiler here -- but the bounds
reasoning is the part worth checking.

Worth stating plainly because the rest of this document is about detection
quality, and this is the part where a bug is a remote read of adjacent
memory rather than a missed camera.

## The signature problem

Here is the part that decides the shape of the work.

**There are no fingerprint values to ship.** I have none, because that needs
a capture from a real camera and nothing in this project has been near one.
They cannot be taken from `FlipDeFlock` either: it is GPL-3.0-or-later and
Pueo is MIT, so its tables are not ours to read values out of. Reimplementing
the technique from the 802.11 spec is fine. Copying its data is not.

So `kIeSigs[]` ships empty, and the first useful thing built is not a
detector but a way to see fingerprints at all:

- the hash shown on the hit row, so it can be written down
- a capture mode that appends `fingerprint, rssi, channel, mac, ssid` to SD

Then a signature table is something an operator builds by standing near a
known camera, which is the only honest way to arrive at one. It also matches
what `SpotterSignatures.h` already says about itself: the signatures are the
part that goes stale, and they are meant to be edited.

## What works with no signatures at all

This is the argument for doing it in this order rather than waiting for
captures.

**Randomised-address flagging** is free and immediate. `src[0] & 0x02` says
the transmitter address is locally administered. That alone is worth
surfacing -- not as a detection, but as a note on the row, because it is
exactly the case where the OUI beneath it means nothing.

**Grouping by fingerprint** fixes the table-exhaustion problem whether or not
any signature matches. Several MACs sharing one fingerprint is one device
rotating its address. Collapsing those into a single row is a straight
improvement to a 48-row table: 500 rotations of one radio now leave 47 rows
free, where before they would have filled it eight times over.

The fold is narrow on purpose, because getting it wrong under-reports
surveillance hardware, and that is the failure that matters here. Two
cameras of the same model have the same element set, so merging on
fingerprint alone would show one device where there are two. The rule
therefore requires that **both** addresses are locally administered: a
globally unique MAC is a real identifier, and a device using one is not
hiding, so it keeps its own row whatever it shares with its neighbours.

It is still not proof. Two handsets of the same model, both randomising,
are indistinguishable from here and will be merged. The fold is limited to
the case where the alternative -- a row per address -- is certainly wrong.
`tools/check_spotter_merge.py` pins all of that down: sixteen rules, one of
which is that two real cameras of a model stay two rows.

**Persistence** is the cheap behavioural signal. A phone walks past. A
camera is bolted to a pole and is still there on the next pass. Spotter
already records `firstMs`, `lastMs` and `hits`; a device seen across many
channel dwells over a long window is camera-shaped in a way a passing handset
is not.

None of those three needs a single captured fingerprint, and all three are
useful on their own.

## Risks and unknowns

**Collisions with commodity hardware.** Phones and IoT devices produce a
small number of very common fingerprints. Some of those will be near-enough
universal, and a table that matches one of them would be worse than no table.
This needs a suppression list built by observing ordinary traffic, and that
list is environment-specific.

**Stability across camera firmware.** A vendor update that changes the
supplicant changes the fingerprint. Unknown how often that happens; it is the
same staleness the OUI tables have, moved to a different field.

**Dwell versus probe interval.** At 260 ms per channel across 13 channels, a
given channel is watched about 8% of the time. The `flock-you` teardown puts
the QCA9377 at roughly 125 ms between probes, which should be caught, but the
hop schedule is a tuning question once there is anything to tune against.

**The whole thing is untested, like everything else here.** This is a design
against a published frame format, written by someone who has not yet seen the
hardware it is meant to detect.

## Phasing

1. **Done.** Restructure `onPacket` to walk elements once; keep behaviour
   identical. Fix the wildcard-probe drop. No new features.
2. **Done.** Hash, `Hit.fingerprint`, and the hash on the UI row. 308 bytes
   of flash, 192 of RAM, which is the 48 rows times the four bytes. The
   fingerprint annotates rows that a signature already created; it does not
   create rows of its own, or 48 passing handsets would fill the table
   before anything interesting arrived.
3. **Done.** Grouping by fingerprint, randomised-address flagging,
   persistence. Another 308 bytes of flash and 192 of RAM. The row's third
   line now reads `fp 25567F65 rnd +3 12m`: the hash, whether the address is
   made up, how many times it has changed underneath us, and how long the
   device has been in range. None of it scores into the confidence -- a
   device being persistent is displayed, not believed, because what
   persistence is worth is a question for hardware to answer.
4. SD capture mode.
5. `kIeSigs[]`, populated from captures, once there are any.

Steps 1 through 3 stand on their own. Step 5 is the only one that needs a
camera, and it is deliberately last.
