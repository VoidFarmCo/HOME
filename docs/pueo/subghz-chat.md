# SubGHz Chat (CC1101)

A broadcast text chat over the CC1101 sub-GHz radio at 433.92 MHz. No LoRa and
no WiFi: it uses the radio already on the hat, peer to peer, with no pairing.
Every message is heard by every nearby H.O.M.E unit on the same net.

It lives at **SubGHz, SubGHz Chat**.

## Using it

1. Open **SubGHz, SubGHz Chat**. It starts listening on 433.92 MHz and shows
   your short name (`H-xxxx`, from the chip ID).
2. Press **Type** (left slot) to open the keyboard, write a message, and **Send**.
3. Messages from other units appear in the log as `name: text`. Your own are
   drawn in the accent colour.
4. **Exit** leaves and idles the radio.

Range is sub-GHz, so it carries further and through walls better than a 2.4 GHz
option would, at the cost of a lower data rate (text only, which is all this is).

## How it works

The CC1101 runs in **packet mode** (`setCCMode(1)`), 2-FSK at 4.8 kbps, with a
fixed **H.O.M.E sync word** (`setSyncWord(0x48, 0x4D)`) so only our units hear
each other, and **CRC on** so corrupt frames are dropped in hardware. Each packet
is `[nameLen][name][text]`, capped well under the 64-byte FIFO.

The receive path treats every packet as untrusted: it reads the name length from
byte 0 and bounds it (1 to `CHAT_NAME_MAX`, and `nameLen + 1 <= len`) before
copying, and clamps the text length to its buffer, so a crafted frame cannot read
past the end. All chat state (the log, the name) is one heap struct, allocated
when the feature opens and freed on exit, because this board's DRAM is full.

## License note

This was written fresh against the CC1101 driver already in the tree, not copied
from Bruce's LoRa chat (Bruce is AGPL; copying it would change H.O.M.E's license).

## What a check holds

`tools/check_subchat.py` pins: packet mode on, the H.O.M.E sync word, CRC on, the
receive-side length bounds, and the SubGHz menu dispatch. Each was broken on
purpose to confirm the check fails.
