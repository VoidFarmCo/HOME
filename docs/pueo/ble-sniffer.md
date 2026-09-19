# Sniffer, and the three things it claims

`Sniffer` is upstream's, on page 0 of the Bluetooth menu. It lists BLE
devices with RSSI and packet counts, and raises two kinds of alert:
**Jamming Suspected** and **MAC Spoofing Suspected**. It is also built to
scan Bluetooth Classic alongside BLE.

All three are worth a closer look, and none of them survives it. Nothing
below has been changed; the code is annotated and this records why.

## "Jamming Suspected" fires on ordinary devices

```c
if (device.packetCount > Config::maxPacketCount ||
    (device.isBLE && device.rssi > Config::minRssiThreshold))
```

with `maxPacketCount = 20` and `minRssiThreshold = -20`.

`packetCount` is set to 1 when a device is first seen and incremented on
every advertisement after. **It is never reset.** A beacon advertising at
the usual 100 ms interval passes twenty in about two seconds, and is then
reported as a suspected jammer for the rest of the session. Every persistent
device in range ends up flagged.

The second half is not a jamming test at all. −20 dBm is a strong signal,
which is to say something close: a phone on the same desk clears it easily.

A real jamming heuristic would be about the noise floor, or about a device's
rate relative to a baseline, in a window rather than a running total. The
thresholds are left alone because picking better numbers without a radio to
watch is guesswork, and guessing is how these ones got here.

## "MAC Spoofing Suspected" cannot detect a MAC change

Two separate problems, and the second is the fatal one.

**The address-class test is the wrong way round.**

```c
return (value & 0xC0) == 0xC0;
```

Top bits `11` is a *static random* address: random, then fixed for the life
of the device. It is the one random class that does not rotate. The
addresses that do rotate are resolvable private addresses, top bits `01`,
which phones and tags use precisely so they cannot be followed — and this
test misses them entirely.

**The counter does not count address changes.**

```c
if (sniffer.devices[i].mac == mac && ...) { idx = i; break; }
...
if (sniffer.isRandomizedMac(mac)) devices[idx].macChangeCount++;
```

The row was found by matching that exact address, so by construction the
address has not changed. What is counted is advertisements from a device
that happens to have a random address. A device that genuinely rotates fails
the lookup and gets a **fresh row with the counter at zero**, so the one
event the feature is named for is the one it can never observe.

With `maxRandomizedMacChanges = 5`, the alert means "six advertisements from
a static random address" — well under a second of normal traffic.

Detecting rotation needs something that survives the address changing. That
is the same problem Spotter has with ALPR cameras, and the answer there was
to fingerprint what does not change; see `docs/pueo/ie-fingerprinting.md`.
The BLE equivalent would be advertisement contents rather than the address.

## The Bluetooth Classic half cannot run

`processNewDevice(BLEAdvertisedDevice*, esp_bt_gap_cb_param_t*, …, bool
isBLE)` takes both a BLE device and a Classic one, `DeviceInfo` carries
`isBLE`, and `btCallback` handles `ESP_BT_GAP_DISC_RES_EVT`. Half of every
device record is for a radio this firmware does not have.

Nothing calls `esp_bt_gap_register_callback` or
`esp_bt_gap_start_discovery` anywhere in the tree, and nothing could: NimBLE
has no Classic support, and `ensureBleStackReady()` hands the Classic
controller RAM back with `esp_bt_controller_mem_release` before the stack
comes up, to reclaim about 30 KB. Same wall the skimmer hunter runs into —
see `docs/pueo/skimmer-hunter.md`.

Kept rather than deleted, because it is one design with `isBLE` and the
`btDevice` argument and pulling one thread unravels a structure somebody may
want if the stack question is ever answered. Marked so that nobody reads the
menu and believes this scans Classic.

## It transmits

`setActiveScan(true)`. Worth stating because three features in this menu
listen and only Spotter is passive. For a general sniffer, active scanning
at least buys something — scan responses carry names — unlike in AirTag
Sniffer, where it bought nothing and has been turned off.

## What it is actually good for

A live list of nearby BLE devices with addresses, names, service UUIDs,
iBeacon UUIDs, RSSI and how often each is heard. That part works and is
useful. It overlaps heavily with **BLE Scanner**, and neither name says how
they differ.

The two alerts are the part to distrust. On current thresholds they are not
detections; they are a count of how long you have been standing there.
