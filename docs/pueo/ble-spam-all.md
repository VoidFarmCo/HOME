# BLE Spam All (Spoofer)

The BLE Spoofer advertises one fixed device at a time (pick a type with Left /
Right, start with Up). **Spam All** is one more device type at the end of that
cycle: instead of a single device, it rotates the advertised payload through
**every** type on a timer, so one run hits Apple, Samsung, Google, Windows
(Swift Pair) and Flipper targets together.

It is a mode of the existing Spoofer, not a new menu tile, so it reuses all the
per-vendor packet builders and the address rotation already there.

## Using it

1. Open **Bluetooth, BLE Spoofer**.
2. Cycle the device type (Left / Right) to **Spam All** (it is after Flipper Zero).
3. Press **Up** to start advertising. The payload now cycles Apple -> Samsung ->
   Google -> Windows -> Flipper and round again, every ~300 ms, while the random
   address keeps rotating on its own clock.
4. Press **Up** again to stop, or exit.

## How it works

`deviceType` runs 1..23 for the real devices; **24 = Spam All** (`SPOOF_TYPE_ALL`).
`setAdvertisingData()` sets `s_spamAll` when the type is 24 and seeds the first
payload. While advertising with the flag set, `spooferLoop()` advances a tick
`(tick % SPOOF_TYPE_MAX) + 1` every `SPAM_ROTATE_MS`, re-applies that real type
with `applySpoofType()`, and re-sets the advertisement. The tick stays within the
real types (1..23), so it never tries to advertise "Spam All" as if it were a
device, and never lands on type 0.

The per-vendor packets are unchanged; this only sequences them. All the honesty
notes in the Spoofer (non-connectable broadcast, rotating random address) still
apply.

## What a check holds

`tools/check_ble_spam_all.py` pins: the pseudo-type is armed on `SPOOF_TYPE_ALL`
and reachable by the device-type cycle; the loop rotates and re-sets the
advertisement; the rotation stays within the real types; and it has a screen
label. Each was broken on purpose to confirm the check fails.
