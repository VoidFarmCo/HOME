# EAPOL capture

Scope for recognising WPA handshakes in frames this firmware can already
record. Nothing here is built.

Split deliberately into a passive half and an active one, because they are
different decisions and only the first is in the spirit of what Spotter
already does.

## Why it is worth doing here

`HaleHound-CYD` advertises "EAPOL Capture -- WPA 4-way handshake + PMKID
extraction". That project ships a README and a LICENSE and no source, and
the ESP32-DIV it derives from has no EAPOL anywhere in its tree. So the
feature exists in this lineage as a claim rather than as code.

`ESP32Marauder` does implement it, is **MIT licensed** (Copyright (c) 2020
Just Call Me Koko), and is therefore usable here with attribution. Reading
it is where most of the design below comes from, along with the part not to
copy.

Worth correcting one thing while it is in view: Marauder does not extract
PMKIDs on the device. `force_pmkid` selects a deauth-active scan mode; the
extraction happens offline from the pcap. Anything claiming on-device PMKID
extraction in this family of firmware is overstating it.

## What already exists here

More than expected, which is the reason this is a small job.

`wifi.cpp` carries a **complete pcap writer**, owned by Packet Monitor: a
radiotap header (DLT 127) with channel, RSSI and MCS, pcap global and record
headers, a slot pool behind two FreeRTOS queues so the promiscuous callback
hands off rather than writing, SD output to `ptm_NNNN.pcap`, drop counting
and periodic flush.

And Packet Monitor already passes data frames to it. Its callback rejects
only `WIFI_PKT_MISC`, so `WIFI_PKT_DATA` goes straight to the queue. **A
handshake that happens while Packet Monitor is recording is probably already
in the pcap.** The gap is recognition and workflow, not capture.

## Two things that have to be fixed first

Both found while scoping, both invisible until looked for.

**The promiscuous filter is global, and Packet Monitor never sets it.**
`esp_wifi_set_promiscuous_filter` is called in exactly two places, both in
`wifi.cpp`, and both set `WIFI_PROMIS_FILTER_MASK_MGMT`. Packet Monitor sets
a callback and enables promiscuous mode without touching the filter, so it
inherits whatever the last feature left behind. Enter the captive portal or
the probe sniffer, leave, then enter Packet Monitor, and it silently sees
management frames only -- no data frames, no EAPOL, no error. Any EAPOL mode
has to set the mask it wants, explicitly, every time it starts.

**The snapshot length is 512 on this board.** `ESP32DIV_PCAP_SNAP_LEN` is
2324 on the ESP32-S3 and 512 everywhere else, and Pueo is a CYD. That is
comfortably enough for EAPOL -- the largest key message runs to roughly 200
bytes including headers -- but it is a ceiling worth knowing about before
someone concludes frames are being mangled.

## Finding the EAPOL frame

This is the part where Marauder should not be copied.

It tests two fixed offsets for the `0x888E` ethertype:

```c
if (snifferPacket->payload[30] == 0x88 && snifferPacket->payload[31] == 0x8e)
  eapol_offset = 32;
else if (snifferPacket->payload[32] == 0x88 && snifferPacket->payload[33] == 0x8e)
  eapol_offset = 34;
```

Two problems. There is **no length guard** -- those four bytes are read
without checking that `sig_len` reaches 34, so a runt frame reads past the
buffer. And 30 and 32 are the only two header layouts it knows: a 24-byte
header plus LLC/SNAP, or a 26-byte QoS header plus the same. Anything else
is missed.

The header length is computable from the frame control field, so compute it:

| condition | bytes |
|---|---|
| base | 24 |
| To DS **and** From DS both set (4-address) | +6 |
| QoS data (subtype bit `0x08`) | +2 |
| Order bit set on a QoS frame (HT Control) | +4 |

Then LLC/SNAP is 8 bytes, with the ethertype in the last 2: the check is at
`hdr + 6`, and the 802.1X payload starts at `hdr + 8`. Every step bounded
against `sig_len` before it is read, the same discipline as the information
element walk, and for the same reason -- this is a buffer off the air.

Only type Data (frame control type bits `0b10`) needs considering. Null-data
subtypes carry no payload and can be dropped early.

## Telling the four messages apart

This part of Marauder is sound, is about twenty lines, and is the
non-obvious bit. The 802.1X Key Information field sits at `eapol + 5`, big
endian, and three of its bits separate the messages:

| | Key Ack (bit 7) | Key MIC (bit 8) | Secure (bit 9) |
|---|---|---|---|
| M1 | yes | no | no |
| M2 | no | yes | no |
| M3 | yes | yes | yes |
| M4 | no | yes | yes |

Marauder does bounds-check this one (`key_info_offset + 1 < len`). Credit
where it is due, and attribution where it is required.

## What to keep, and where

Per access point rather than per frame, because the useful question is "do I
have a usable handshake for this network", not "how many EAPOL frames went
past".

- which of M1 to M4 have been seen, as four flags
- the station address the handshake was with
- when the last message arrived, so a stale partial can be distinguished
  from one in progress

M2 plus M3 is the pair worth having. M1 alone is worth keeping too, because
that is where a PMKID would be if there is one, but see the note above about
extraction being an offline job.

**The beacon has to be in the capture as well.** The tools that consume
these want the ESSID, and it does not appear in the handshake. Packet
Monitor records beacons already; a dedicated mode has to be careful not to
filter them out in the name of tidiness.

## The active half, which is a separate decision

Handshakes happen at association, so waiting for one passively means waiting
for somebody to connect. Every tool in this space forces the issue by
deauthenticating a client so it reconnects -- Marauder sends five deauth
frames each time it sees a beacon of the target.

Pueo already has a deauther, so the capability is not new. What would be new
is wiring it to a capture, and that is worth deciding rather than defaulting
into. The passive half is a receiver. The active half transmits at other
people's equipment to make it drop off its network, which is a different act
legally and a different act ethically, and this project's own pages tell
people to use it only on networks they are allowed to touch.

Recommendation: build the passive recogniser first, and keep any deauth
pairing behind an explicit mode with its own confirmation, rather than a
setting that quietly changes what the feature does.

## Testing it without hardware

The same approach as `tools/fuzz_ie_walk.py`, and for the same reason: none
of this can be tried on a board that does not exist yet.

- synthesise frames for each header layout -- 3-address, 4-address, QoS,
  QoS with HT Control -- and assert the ethertype is found at the right
  offset in each
- synthesise the four key messages and assert each classifies correctly
- fuzz truncated and malformed frames through a buffer that refuses any read
  outside them, and assert no out-of-bounds access
- assert a frame that is 33 bytes long does not crash the thing, which is
  the case the fixed-offset version gets wrong

## Unknowns

**Whether ESP32 promiscuous mode delivers data frames whole.** This is the
one that decides whether any of it works, and it cannot be answered from
here. `sig_len` is trusted throughout this firmware, but data frames take a
different path through the driver than management frames and the reported
length may not be the whole MPDU. Worth confirming against a real capture
before writing the recogniser, not after.

**Whether the pcap the existing writer produces is accepted by the offline
tools.** Radiotap DLT 127 is right, but the header this project emits is a
fixed 19 bytes with five presence bits, and a consumer that dislikes it will
say so in a way that is easy to fix and hard to guess.

## Phasing

1. Set the promiscuous filter explicitly wherever data frames are wanted.
   Fixes a live hazard whether or not the rest is built.
2. Header-length parsing and the ethertype check, bounds-checked, with the
   test harness above. No behaviour yet.
3. Key Information classification and the per-AP flags.
4. Show the flags, and record to the existing pcap writer.
5. Only then, and separately, the question of forcing a reassociation.
