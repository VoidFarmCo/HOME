# The three spoofers, and the address none of them changes

`BLE Spoofer`, `Sour Apple` and `AirTag Spoofer` are upstream's, all on page 0
of the Bluetooth menu, and all transmit. They advertise fabricated BLE
packets to make nearby phones show pairing prompts, action prompts, or a
tracker.

They do different things, which the names only half suggest, and they share
one defect.

## What each actually sends

| | Apple type | payload |
|---|---|---|
| **BLE Spoofer** | Continuity `0x07`, proximity pairing | a table of Apple model bytes, plus Samsung (`0x0075`) and Google Fast Pair (`0xFE2C`) templates |
| **Sour Apple** | Continuity `0x0F`, nearby action | a 17-byte packet built per burst |
| **AirTag Spoofer** | offline finding | a 31-byte packet with a random key, plus a model byte |

So BLE Spoofer is the "a device wants to pair" prompt, Sour Apple is the
"action" prompt that gave the technique its name, and AirTag Spoofer
pretends to be a separated tracker. They overlap only in that all three
impersonate Apple.

## The defect: the address is generated and never used

All three build a random BLE address and then do nothing with it.

```c
esp_bd_addr_t dummy_addr = {0x00, 0x00, 0x00, 0x00, 0x00, 0x00};
for (int i = 0; i < 6; i++) {
  dummy_addr[i] = random(256);
  if (i == 0) dummy_addr[i] |= 0xF0;
}
```

That is `BleSpoofer::toggleAdvertising` and, word for word, `SourApple`.
`AirTagSpoofer` has its own version writing `s_mac`, with `|= 0xC0` — the
correct top bits for a static random address, so somebody knew what they
were doing — and then also never applies it.

Nothing in the file calls `esp_ble_gap_set_rand_addr`,
`NimBLEDevice::setOwnAddrType` or `ble_hs_id_set_rnd`. **Every packet all
three send goes out from the ESP32's one fixed BLE address.**

A stream of a dozen different Apple products that all share a single address
is not a disguise. It also makes the transmitting device trivially
attributable for as long as it runs, which matters to whoever is holding it.

**Why the compiler did not say so.** `dummy_addr` is an array written
through subscripts, and GCC's `-Wunused-but-set-variable` does not fire on
that shape. The two dead constants beside it, `SAMSUNG_COMPANY_ID` and
`GOOGLE_FAST_PAIR_ID`, needed `-Wunused-const-variable`, which is not in
`-Wall` or `-Wextra` for C++. A build being warning-clean is not the same as
a build having nothing unused in it.

## What was changed, and what was not

**Changed: AirTag Spoofer no longer displays an address it is not using.**
It printed `s_mac` on screen and in the log as though that were where the
packets came from. The screen now shows the address actually in use. That is
an operator-facing correctness fix — a tool that tells you the wrong thing
about what it is transmitting is worse than one that says nothing.

**Changed: the two dead constants are gone**, and the three unused-address
sites are commented so the next reader does not assume rotation happens.

**Not changed: the address still does not rotate.** Wiring it up would make
these harder to attribute and more convincing, which is an increase in what
the features do rather than a correction to what they claim. That is the
owner's decision, not a tidy-up, so it is written down here instead of made.

If it is wanted, the call is `NimBLEDevice::setOwnAddrType()` with a
non-resolvable private address, or `ble_hs_id_set_rnd()` with the bytes
already being generated. It would also need doing per burst rather than
once, or it is one new fixed address instead of one old one.

## Also worth knowing

All three advertise at a 20 ms interval (`setMinInterval(0x20)`), which is
the floor. That is deliberate for this kind of thing and is also what makes
them conspicuous to anything watching the band.

None of this has run on hardware, like everything else here.
