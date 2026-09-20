# The rotating-tail detector

`AirTagSniffer` keys its rows on the BLE address. That is fine against a
real AirTag and useless against a clone.

The published ESP32 clone — [Fabian Bräunlein's, from
2022](https://positive.security/blog/find-you) — cycles a couple of thousand
public keys so no identity ever persists. Against an address-keyed detector
that means a fresh row per rotation, the hit count resetting to 1, and after
32 identities the list quietly evicting the oldest. **The harder something
works to avoid detection, the less it stands out.** That is the wrong way
round, and it is what this exists to fix.

`ESP32-DIV/TrackerFollow.{h,cpp}` stops asking *have I seen this device
before* and asks *has something been beside me, at the same distance,
wearing a succession of names*.

## What it detects

One thing, narrowly: **identity rotation from a stable distance.**

Sightings of the long form of Continuity `0x12` — a separated tag carrying a
key, which is what a clone emits — go into RSSI lanes 6 dB wide. A lane that
is held without a long gap, while collecting persistent identities fast
enough, is the finding.

| grade | needs | first seen in the scripted clone |
|---|---|---|
| possible tail | 2 min, 3 identities | 2.0 min |
| likely tail | 5 min, 6 identities | 5.0 min |
| rotating tail | 10 min, 12 identities | 10.0 min |

Every grade also needs the rate gate below.

## Two gates, and the test that forced each

The scenarios in `tools/check_tracker_follow.py` are scripted minute by
minute: a desk, a concourse, a clone following, a clone merely present, and
silence. **Three of the five exist only to stay quiet.** Both gates below
were added because a scenario failed, not because they seemed like good
ideas.

### The maturity gate — because a shopping centre read as a tail

The first cut counted every distinct address in a lane. That collapsed
immediately, because a lane is not "one device holding station", it is
"anything at roughly this distance". In a crowd every lane stays occupied by
a parade of strangers, runs accumulate, identity counts climb, and the
concourse scenario produced a confident tail.

What separates them is that a follower's identities **persist**. A clone key
is seen dozens of times before it rotates; a passer-by is seen once and is
gone. So an address only counts toward a lane once it has been seen
`kIdentityMinSightings` times, which is 3.

### The rate gate — because sitting still crept into an alert

With maturity in, the desk scenario passed at half an hour. It would not
have passed at forty-five minutes: a real AirTag rotating on Apple's
schedule reaches two identities in thirty minutes and three in forty-five,
and three was the old threshold. **Sitting at your own desk long enough
would eventually have reported itself.**

A count accumulates; a rate does not. A qualifying lane must average a new
persistent identity at least every four minutes — 0.25 a minute. Apple
rotates at about 0.07 a minute. The clone rotates at 2.0. Thirty times
apart, with the line between them.

## The measured margins

Not estimates — these come out of the scenarios:

| scenario | verdict | rate | vs the 0.25/min line |
|---|---|---|---|
| desk, 45 min | none | 0.07/min | 3.6× under |
| concourse, 20 min | none | 0 identities | n/a, nothing matured |
| clone following | **rotating tail** | 2.00/min | 8× over |
| clone present, not following | likely tail | 0.52/min | 2× over |

There is an explicit check that three hours at a desk is as quiet as thirty
minutes. That is the property the rate gate buys and it is worth pinning,
because the failure it prevents is the one that would make the feature
useless: an alarm that always eventually goes off.

315 checks in total, including a fuzz pass that asserts the verdict stays
inside its enum, churn inside its ring, and lanes inside their array.

## On screen

The tail line sits between the header and the column titles and is **always**
shown — `No tail  3 new/min` when quiet. An answer that only appears when it
is bad teaches you nothing the rest of the time, and this is a feature whose
entire job is telling you whether you are being followed. It costs the list
one row, ten down to nine.

Rows in the lane that raised the verdict get a red bar on the **right** edge.
The left edge is the selection bar, so the two cannot be confused. That is as
specific as this can honestly get: there is no identity to point at, that
being the whole problem.

Churn — new identities a minute — is on the line. Above 20 the verdict draws
orange instead of red. A crowd, or you moving through one, degrades every
signal here at once; the reading is still shown, marked as one to take with
salt rather than silently suppressed.

Addresses are hashed with FNV-1a before the detector sees them, so nothing in
this feature holds a list of who was near you.

## What it does not do

**A plain AirTag following you.** It rotates too slowly to clear the rate
gate, by design. That case is already visible — the sniffer shows it as one
row with a rising hit count — and Apple's own detection covers it. This is
aimed at the thing that defeats both.

**Anything useful while you move through a crowded place.** Lanes fragment,
churn spikes, and the honest output is the busy marker.

**Tell you what the device is.** No key, no address, nothing to hand to
anyone. "Something is doing this, at about this signal strength" is the limit
of one radio with no history.

It is a heuristic. Apple does the equivalent server-side with the whole
network in view. Scored like Spotter: compute it, show it, claim nothing the
signal does not support.

## The assumption most likely to be wrong

**That a passer-by is seen fewer than three times.**

The maturity gate is the thing keeping the concourse quiet, and it rests on
transient devices producing one or two sightings. If a passing device gets
picked up on several advertising channels in quick succession — three
channels, three callbacks, one walk past — it matures, counts, and the crowd
starts feeding the lanes again.

Nothing in Python can settle that. It needs a capture in a real crowd with
the sniffer running, comparing the churn figure against how many identities
actually mature. If they turn out close, the fix is a minimum *spread*
between an identity's first and last sighting rather than a bare count, so
that three hits in 200 ms do not qualify where three hits over a minute do.

The scenarios here are models of crowds, not recordings of them. That is the
gap, and it is the first thing to close on hardware.
