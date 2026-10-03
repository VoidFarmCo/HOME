# The Fast Pair probe (CVE-2025-36911)

This one transmits. It is the only part of the Fast Pair feature that does.

Listening is `docs/pueo/fast-pair.md`. This is the other half: connecting to
one device you picked and writing eighty bytes to it, to find out whether it
answers a handshake it should refuse.

## What the handshake is supposed to do

A Fast Pair Seeker that wants to pair with a Provider:

1. looks up the Provider's **anti-spoofing public key** on Google's servers,
   keyed by the Model ID in the advertisement;
2. generates an ephemeral secp256r1 keypair;
3. does ECDH against the anti-spoofing key, SHA-256s the shared X
   coordinate, keeps the first 16 bytes as an AES key;
4. AES-128-ECB encrypts a 16-byte request whose bytes 2–7 are **the
   Provider's own BLE address**;
5. writes those 16 bytes plus its own 64-byte public key to the Key-based
   Pairing characteristic.

The Provider runs the same ECDH with its anti-spoofing *private* key,
arrives at the same AES key, decrypts, and checks that the address in the
request is its own. Only then does it notify a response.

**That address check is the entire security property.** A Seeker that never
got the real anti-spoofing key derives a different AES key, so the request
decrypts to noise, so the address field is noise, so it cannot match, so a
correct Provider says nothing at all.

## What the probe does

Exactly the above, except that at step 1 there is no anti-spoofing key,
because not having it is the point. What gets used instead is either a
public key the Provider itself exposes on its Model ID characteristic, or,
when it exposes none, the secp256r1 generator point, which is a valid point
on the curve and is named `kGeneratorP256` rather than dressed up as a key,
because it is not one.

Everything else is well formed. The request names the Provider's address
correctly, the ECDH is real, the AES is real, the write is a normal GATT
write with response.

A correct Provider cannot answer. A notification means the address check
did not happen, or happened against a key the Seeker controls.

## Reading a result

Five outcomes, and the two interesting ones are both weaker than they look.

**Responded.** It answered a handshake it could not have validated. That is
the finding. The screen distinguishes three grades: the notification
decrypted with our key and named the Provider's own address (strongest), it
decrypted to a well-formed response naming a different address, or it did
not decrypt at all. The last is still a notification the device should not
have sent, but it is also what a stray notification from an unrelated
characteristic looks like, so confirm it twice before reporting it anywhere.

**No response.** This is what a correct device does. **It is not proof of
safety** and the screen says so in those words. A device is also silent when
it is busy, already connected to its owner's phone, out of range, or when
the link dropped before the notification arrived. The failure mode to worry
about is a probe that is subtly broken (a reversed address, a write to the
wrong characteristic) because then every device reports "no response" and a
broken probe is indistinguishable from a room full of correctly-behaving
devices. That is why `tools/check_fastpair_probe.py` exists and why it spends
most of its 50,293 checks on the address field.

**No Fast Pair service**. It advertised `0xFE2C` but exposes no such GATT
service, or no Key-based Pairing characteristic. Nothing was tested.

**Probe failed**: could not connect, the write was rejected, or the crypto
would not set up. Nothing was tested.

**Refused: stealth mode.** The probe writes, so Stealth Mode stops it.
Nothing was transmitted and nothing was tested. The check is inside
`FastPairProbe::run()` rather than at the button, so the gate is on the
function that does the transmitting and a second caller cannot miss it; the
confirm screen also says so before you confirm, because a device that takes
a deliberate second keypress and then declines looks broken. The scan around
it keeps running: it is passive, and refusing it would be refusing the part
that is already quiet.

## What it does not do

It does not pair. It does not write an account key. It does not
deauthenticate, jam or broadcast. It writes once, to one characteristic, on
one device, and waits four seconds.

It does open a GATT connection, which a device may log and which may briefly
interrupt whatever it was connected to. That is not nothing, and it is why
the probe sits behind a confirm screen that states what it is about to do
and to which address, and why it never runs on its own or against more than
the selected row.

Point it at your own devices or ones you have permission to test.

## Why the scan stops first

NimBLE will not open a connection while a scan is running, so `scanStop()`
is called before `FastPairProbe::run()` and the scan restarts when you
return to the list. This means the device list stops updating for the few
seconds the probe takes, which is the correct trade and worth knowing before
it looks like a freeze.

## What is checked, and what cannot be

`tools/check_fastpair_probe.py` transcribes `buildRequest()` and
`parseResponse()` and runs 50,293 checks: the byte positions of the type,
flags and address fields, that the address is not reversed, both tail
layouts, that nothing is left uninitialised, every message type that is not
a response, and 50,000 random blocks that must never be mistaken for a
matched response.

The probe itself cannot be checked here. It needs a radio and a second
device. The crypto path in particular has never derived a key against a real
Provider, so if every device reports "no response", suspect the probe before
concluding the devices are sound.
