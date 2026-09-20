# The jammers

Three of them, all upstream's: **BLE Jammer** on Bluetooth page 1, **Proto
Kill** in the 2.4 GHz menu, and **SubGHz Jammer** under SubGHz. This is what
each actually puts on the air, which is not quite what the names suggest and
is not the same story for both radios.

Documented to the same standard as `fast-pair-probe.md` because they belong
to the same category — the features that transmit — and that half of the
device should be as legible as the listening half.

## What they transmit

### BLE Jammer and Proto Kill — NRF24, and this part is certain

Both drive one NRF24 through `Nrf24Raw::startConstCarrier()`, which writes:

```c
writeReg(REG_RF_SETUP, RF_CONT_WAVE | RF_PLL_LOCK | RF_DR_HIGH | RF_PWR_MAX);
```

`RF_CONT_WAVE` is the chip's constant-carrier bit. So this is **an
unmodulated carrier at maximum output** — not packets, not noise, not
malformed frames. Pure RF energy on one channel at a time, and on a PA+LNA
module that is roughly +20 dBm.

They differ only in which channels they walk:

| | channels |
|---|---|
| BLE Jammer, BLE mode | 3 — 2, 26, 80, the BLE advertising channels |
| BLE Jammer, Bluetooth mode | 21 across the band |
| Proto Kill | 8 selectable sets: BLE, Bluetooth, WiFi, video TX, RC, USB wireless, Zigbee, NRF24 |

The hop is round-robin with a **4 ms dwell**, not a fresh random pick each
time round the loop. That is Pueo's change and the reason is in the source:
every hop takes CE down, retunes and waits 130 µs for the synthesiser, so
hopping every iteration spent most of the time settling rather than
transmitting. Random also repeats channels while leaving others unvisited.

**The "three radios" were never three.** Upstream configured three RF24
objects over three channel groups, reading as twelve channels across three
modules. It was neither: Pueo maps all three chip selects onto the one
module, and the old `configureRadio()` called `startConstCarrier()` once per
channel, each call replacing the last, so only the final entry of each group
survived. The hopping that actually happened was in the feature loop, and
still is — done once now instead of three times to the same chip.

### SubGHz Jammer — CC1101, and this part is **not** established

The sub-GHz one is configured once and then keyed:

```c
ELECHOUSE_cc1101.setModulation(0);   // 2-FSK
ELECHOUSE_cc1101.setPA(12);          // +12 dBm
ELECHOUSE_cc1101.setMHZ(targetFrequency);
ELECHOUSE_cc1101.SetTx();
```

`SetTx()` puts the chip in TX. **Nothing is ever fed to it.** There is no
payload written to the FIFO, and `TX_PIN` — the GDO0 line that would carry
data in an asynchronous-serial setup — is only ever driven LOW, on stop. It
is never toggled while jamming.

So what radiates is an open question. A CC1101 in TX with an empty FIFO
underflows; depending on how the packet engine is left configured it may sit
on an unmodulated carrier at the deviation centre, emit a preamble and then
stop, or produce very little at all. **Unlike the NRF24 path there is no
constant-wave bit set here, so nothing in the code states the intent.**

That is worth knowing before relying on it, and it is the single most
useful thing a spectrum analyser or an SDR would settle about this device.
Until then the honest description of SubGHz Jammer is "keys the transmitter
on the selected frequency", not "jams it".

Frequency comes from the shared `subghz_frequency_list`, stepped manually or
swept every 1000 ms in auto mode.

## There is no success indicator, and there cannot be one

None of the three can tell you whether they worked. A jammer transmits and
watches nothing; the screen shows only that the feature is running. That is
not an omission to be fixed — to know you had suppressed a link you would
have to be receiving it, which is a different radio configuration and a
different feature.

So "Jamming" on screen means the transmitter was keyed. It does not mean
anything nearby stopped working, and nothing here will ever tell you that it
did. The Jamming Detector under SubGHz is a separate receive-only feature
and is not a companion to this.

## Where this sits legally, briefly

Worth stating once because it is genuinely different from the rest of the
device, not as a warning.

Every other transmitting feature here — the spoofers, the Fast Pair probe,
the deauth logic that is deliberately not wired up — is a thing where
consent is a real defence. Test your own devices, or ones you have
permission to test, and you are doing authorised security work.

Deliberate radio interference is generally an offence in itself in most
jurisdictions, and permission from the owner of the jammed device usually
does not change that, because the protected interest is the spectrum rather
than the device. That is why these features sit apart from the others in
this documentation even though they came in the same fork.

The practical answer, and the one this project assumes, is that transmit
testing happens **inside a Faraday cage or shielded enclosure**. That makes
the question moot, and it is also the only environment where the results
mean anything, since there is no way to tell from the device whether
anything was affected.

## Not verified

None of the three has been observed transmitting. Specifically:

- the NRF24 carrier is verified at the **register level only**: the
  constant-wave bit is set and the power bits are at maximum, which is what
  the datasheet says produces a carrier. Nobody has seen it on an analyser.
- what the CC1101 emits is **not established at all**, as above.
- the 4 ms dwell was chosen from the datasheet's 130 µs synthesiser settling
  time, not measured.
