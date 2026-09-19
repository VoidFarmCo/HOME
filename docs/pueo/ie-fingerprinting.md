# Probe-request IE fingerprinting

Scope for a Spotter detection path that survives MAC randomisation. Nothing
here is built yet. This is the design and, more importantly, the reason the
signature table has to start empty.

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
| `0x00` | SSID | **no** |
| `0x01` | Supported Rates | yes, with contents |
| `0x03` | DS Parameter Set | **no** |
| `0x2D` | HT Capabilities | yes, with contents |
| `0x32` | Extended Supported Rates | yes, with contents |
| `0x7F` | Extended Capabilities | yes, with contents |
| `0xBF` | VHT Capabilities | yes, with contents |
| `0xDD` | Vendor Specific | yes, OUI and type only |
| `0xFF` | Element Extension | yes, ext id only |

The fingerprint is a hash over that sequence, in the order the elements
actually appear, because order is itself part of the signature.

Two exclusions matter more than any of the inclusions.

**SSID must not be in the hash.** It is the thing that varies between two
probes from the same device, and it is already matched separately. Including
it would produce a fingerprint per network name rather than per device.

**DS Parameter Set must not be in the hash.** It carries the current
channel, and Spotter hops channels every 260 ms. Including it would give the
same camera up to thirteen different fingerprints.

For vendor-specific elements only the 3-byte OUI and the vendor type go in,
not the payload. The payload of a WPS or Apple element carries device state
that changes between probes; the fact that the element is present, and whose
it is, does not.

FNV-1a over that canonical byte sequence gives a `uint32_t`. The choice of
hash is not interesting -- it needs to be cheap and well distributed, not
cryptographic.

## Where it lands

`onPacket` in `Spotter.cpp`, and it needs that function restructured before
it can go in.

Today the flow is: match the OUI, then find the SSID element, then match the
SSID. The SSID step bails on a zero-length SSID:

```c
if (ssidLen == 0 || ssidLen > 32 || len < (uint16_t)(ieStart + 2 + ssidLen)) {
  return;
}
```

That is correct for what it does. It is also fatal for fingerprinting,
because a zero-length SSID is a **wildcard probe request** -- a device asking
"is anyone there" rather than "is *my* network there" -- and those are a
large fraction of the probes in the air. They carry the full element set.
Dropping them throws away most of the evidence.

So the walk has to happen first and the SSID match becomes one case inside
it:

1. Walk the elements once, bounds-checked.
2. Accumulate the hash as described.
3. If element `0x00` is present and non-empty, run the existing SSID match.
4. After the walk, match the hash against the fingerprint table.

One pass, no second traversal.

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

This walks an attacker-shaped buffer. Every step needs `id`, `len` and
`value` checked against `sig_len` before it is read, and the loop needs a
hard iteration cap so a malformed frame cannot spin it. The existing SSID
code gets this right and the walk has to be held to the same standard --
`len` is a single byte from the air and `ieStart + 2 + len` can run past the
end of a truncated frame.

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
any signature matches. Several MACs sharing one fingerprint, appearing and
disappearing together, is one device rotating its address. Collapsing those
into a single row is a straight improvement to a 48-row table.

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

1. Restructure `onPacket` to walk elements once; keep behaviour identical.
   Fix the wildcard-probe drop. No new features.
2. Hash, `Hit.fingerprint`, and the hash on the UI row.
3. Grouping by fingerprint, randomised-address flagging, persistence.
4. SD capture mode.
5. `kIeSigs[]`, populated from captures, once there are any.

Steps 1 through 3 stand on their own. Step 5 is the only one that needs a
camera, and it is deliberately last.
