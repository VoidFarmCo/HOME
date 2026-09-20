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
| **AirTag Spoofer** | Continuity `0x07`, proximity pairing, prefix `0x05` | a 31-byte packet with a model byte and 16 random bytes |

So BLE Spoofer is the "a device wants to pair" prompt, Sour Apple is the
"action" prompt that gave the technique its name, and AirTag Spoofer is the
"a new AirTag is nearby, set it up?" prompt.

**AirTag Spoofer does not pretend to be a separated tracker**, which is the
easy assumption to make from the name and which this document got wrong
until it was checked against `buildProximityPacket()`. A separated AirTag
broadcasts Continuity `0x12`, offline finding, carrying a rotating public
key that the Find My network relays. This sends `0x07` proximity pairing
with prefix `0x05`, which is the setup popup for an *unpaired* tag. The
sixteen random bytes are payload padding, not a Find My key.

Nothing here transmits `0x12` at all. The only code in this firmware that
touches offline finding is `AirTagSniffer`, which **receives** it &mdash; that
is what lets it tell a separated tag from a passing phone.

## The defect, and the fix

All three used to build a random BLE address and then do nothing with it.

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
were doing — and then also never applied it.

Nothing in the file called `esp_ble_gap_set_rand_addr`,
`NimBLEDevice::setOwnAddrType` or `ble_hs_id_set_rnd`, so **every packet all
three sent went out from the ESP32's one fixed BLE address.**

A stream of a dozen different Apple products that all share a single address
is not a disguise. It also made the transmitting device trivially
attributable for as long as it ran, which matters to whoever is holding it.

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

**Changed: the address now rotates.** This was left alone at first and
written up here instead, because making the spoofers harder to attribute is
an increase in what they do rather than a correction to what they claim, and
that is the owner's call. It was made.

Three decisions worth recording.

**Non-resolvable private, not static random.** `ble_hs_id_gen_rnd(1, ...)`
asks the stack for an NRPA, which is the address type the specification
defines for exactly this. A static random address is meant to hold still for
a power cycle, so rotating one is out of spec even though the controller
allows it. Letting the stack generate it also sidesteps the top-two-bits
rule, which the old dead code got right once (`0xC0`) and wrong twice
(`0xF0`, and on the wrong end of the array for NimBLE, which takes the
address little-endian).

**Once a second, not once a burst.** The controller refuses
`HCI_LE_Set_Random_Address` while advertising is enabled, so a rotation is
stop, set, start. `AirTagSpoofer` carries a note that repeated stop/start was
resetting the board, so rotating on every 40 ms burst is the exact pattern to
avoid. A second is far below anything that makes a device followable and is
about twenty-five times less churn.

**It gives up rather than going dark.** Rotation is stop, set, start, and
if the board turns out to dislike that the failure has to be visible. Three
consecutive failures latch it off and put one line on screen saying which
step was refused &mdash; `stop refused`, `set addr refused`, `restart refused`.
Advertising is restarted whatever happened in between, because a spoofer
that silently stops transmitting is worse than one transmitting from a stale
address, and a stale address is exactly what every release before this did.
Entering a spoofer resets the latch, so a transient refusal does not disable
rotation for the session.

**The screen reads the address back from the stack.**
`NimBLEDevice::getAddress()` is no use here: it prefers the *public* address
and only falls back to random when there is no public one, which on an ESP32
never happens. It would confidently display the one address the radio is not
transmitting from. `ble_hs_id_copy_addr(BLE_ADDR_RANDOM, ...)` gets the real
one.

## Also worth knowing

All three advertise at a 20 ms interval (`setMinInterval(0x20)`), which is
the floor. That is deliberate for this kind of thing and is also what makes
them conspicuous to anything watching the band.

None of this has run on hardware, like everything else here.

## Two templates added: Swift Pair and Flipper Zero

`BLE Spoofer` had Apple, Samsung and Google. It now also has Microsoft
Swift Pair and Flipper Zero, as device types 22 and 23.

Both were built from primary sources rather than from another tool's byte
arrays, which matters because in both cases the primary source said
something the copies do not.

### Swift Pair

Windows raises a "New *name* found" notification for any LE advertisement
carrying Microsoft's vendor section:

```
02 01 06                     flags
LL FF 06 00 03 SS 80 <name>  vendor-specific
```

From Microsoft's Swift Pair component guidelines. The vendor ID `0x0006`,
the `0x80` reserved RSSI byte, and the sub-scenario table are stated in the
text. **The Beacon ID `0x03` is not** — it appears only in that page's
figures, which are images. It is what implementations use and what Windows
answers, but it was read off a picture, and that is worth knowing if it ever
stops working.

The sub-scenario values the spec defines:

| value | meaning |
|---|---|
| `0x00` | pairing over Bluetooth LE only |
| `0x01` | pairing over BR/EDR only, using LE for discovery |
| `0x02` | pairing over LE and BR/EDR with Secure Connections |

Only `0x00` is sent. The other two describe a dual-mode peripheral, and
`0x01` additionally requires the BR/EDR address in the same advertisement.
This device transmits LE and has no BR/EDR address to offer, so sending
either would advertise a capability that is not there. Several spam tools
send `0x03`, which is not in the table at all.

**The name is `Pueo-XXXX`, deliberately not a real product's.** Windows
prints it verbatim. A neutral name means whoever is running the test can
tell their own notification from a stranger's, and nothing here impersonates
a brand it has no business wearing. The four random characters exist because
the BLE address never rotates — see above — so the name is the only thing
that makes a second burst appear as a second notification.

### Flipper Zero

```
02 01 06                     flags
LL 09 <name>                 complete local name
03 02 <lo> 30                incomplete list of 16-bit service UUIDs
02 0A 00                     TX power
```

The service UUID is `0x3080` with the hardware colour OR'd into the low
bits. That is not inferred from captures — Flipper's own firmware does it,
in its serial profile:

```c
.Service_UUID_16 = 0x3080
config->adv_service.Service_UUID_16 |= furi_hal_version_get_hw_color();
```

So `0x3081`, `0x3082` and `0x3083` are one service on differently coloured
hardware, not three services. The colour is randomised per burst, skipping
0, which is the uncoloured value no unit in the wild advertises.

### A dead line found next door

`devices_uuid` in `bluetooth.cpp` is `00003082-0000-1000-9000-00805f9b34fb`
— the Flipper UUID, which is where the number above was first noticed. It is
passed to `addServiceUUID()` immediately before `setAdvertisementData()`, and
a custom advertisement payload replaces whatever `addServiceUUID` built, so
it never reaches the air. It also carries `9000` where the Bluetooth base
UUID has `8000`.

Left alone. Removing it should change nothing, but "should change nothing"
is analysis rather than a measurement, and this change was about adding two
templates. It is written down here so the next person does not spend the
same half hour on it.

### What is checked

`tools/check_ble_adv.py` builds both packets and walks them the way a
receiver does, insisting the walk lands exactly on the end of the packet.
That is the check worth having: a wrong length byte still transmits, still
looks right in the log, and is silently dropped by everything that hears it.
1095 checks, covering every name and colour either builder can produce.

It also walks the three older templates, which are fixed arrays and had
never been checked. They are all well formed. The Google one decodes as
three bytes of Fast Pair service data, which by the rule in `FastPair.cpp`
is a discoverable frame advertising Model ID `00B727` — one fixed model,
never varied.
