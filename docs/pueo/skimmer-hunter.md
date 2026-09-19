# The skimmer hunter, and what it cannot see

`Skimmer Detect` is upstream's, arriving with ESP32-DIV v1.7.2. It scans for
the default names of the serial modules that turn up inside Bluetooth-enabled
card skimmers -- HC-05, HM-10, JDY-08 and their relatives -- and rates each
1 to 5. A hit means a suspicious module is nearby, not that a skimmer is.

This note exists because the signature table promises more than the scanner
delivers, and nothing in the feature says so.

## It scans BLE only

`BleSkimmer` calls `BLEDevice::getScan()` and nothing else. There is no
Bluetooth Classic discovery anywhere in the tree: `esp_bt_gap_start_discovery`
and `esp_bt_gap_register_callback` appear nowhere.

Eleven of the twenty-eight signatures are Classic-only modules, and they
include **seven of the eight rated 5**:

| Reachable by a BLE scan | Not reachable |
|---|---|
| HC-08, HM-10, HM-11, HM-19, AT-09, CC41, CC41-A, MLT-BT05, BT-05, BT-06, JDY-08, JDY-10, JDY-16, JDY-23 | HC-03, HC-04, **HC-05**, **HC-06**, BT-HC05, BT-HC06, FREE2MOVE, LINVOR, BT-SPP, SPP-CA, JDY-31 |

HC-05 and HC-06 are the modules most associated with skimmers in the first
place. They speak Bluetooth Classic SPP and do not advertise over BLE, so
this feature will sit next to one and report nothing.

Three more -- BT-04, BT04-A, BT-08 -- are sold in both flavours under the
same name, and the table cannot tell which is in front of it. They are marked
`Both` rather than guessed at.

## Why it is not a small fix

Two separate blockers, either of which is enough on its own.

**NimBLE has no Bluetooth Classic support.** The whole firmware is built on
it: `BleCompat.h` aliases `BLEDevice` and the rest onto the NimBLE types.

**The Classic controller RAM is deliberately given away.**
`ensureBleStackReady()` calls
`esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT)` before initialising
the stack, to reclaim about 30 KB and avoid an out-of-memory reboot after the
intro screen on a classic ESP32. That cannot be undone without a reboot.

So reaching the Classic modules means Bluedroid instead of NimBLE, on a build
already at 85% of flash, with the heap reclaim reversed. That is not a
signature-table change; it is a different Bluetooth stack.

There is a matching piece of dead code: `BleSniffer` carries a
`btCallback(esp_bt_gap_cb_event_t, ...)` that handles Classic discovery
results. Nothing registers it and nothing could, for the same two reasons.

## Two other things worth knowing

**It transmits.** `setActiveScan(true)` sends scan requests, which is the
normal way to get names out of BLE devices and the opposite of Spotter's
posture. Worth stating because the two features sit near each other and one
of them is advertised as receive-only.

**The table's order is load-bearing.** Matching is `strstr` against the
normalised name with the first hit winning, so a longer needle must precede
any shorter one it contains: `BTHC05` before `HC05`, `MLTBT05` before `BT05`,
`CC41A` before `CC41`, `BT04A` before `BT04`. The existing order is correct.
Adding a signature in the wrong place does not fail, it silently relabels
devices, which is why the table now says so above itself.

## What was done

The signatures carry a `Radio` column and the header explains the limit, so
the next person to read the table sees what it can and cannot reach. The
entries were kept rather than removed: they are correct about what a skimmer
contains, and they are what a Classic-capable build would need.

Nothing about the scanning changed.
