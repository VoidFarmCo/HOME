# Station scanner (WiFi Scanner clients)

The WiFi Scanner lists access points. Open one (tap it for the detail view) and
the **Stations** slot sniffs for the clients talking to that AP, the way
Marauder and HaleHound do it: an AP scan first, then a sniff that pairs clients
to a known AP.

It is a mode of the WiFi Scanner, not a separate menu entry. The WiFi grid is
full at twelve tiles, and the scanner already has the AP list and the selection
a station scan needs, so chaining off it reuses both.

## Using it

1. Open **WiFi, WiFi Scanner**. Let it scan, then tap an AP to open its detail.
2. Press **Stations** (the up slot). The body switches to a client list for
   that one AP: MAC, signal, and how many seconds since it was last heard.
3. Press **Back** to return to the AP detail, or **Exit** to leave.

It pins the radio to the AP's own channel rather than hopping, so it catches
more of that one AP's traffic than a channel sweep would. The trade is that it
sees one AP at a time, which is the point: you picked the AP.

## How it works

Entering snapshots the selected AP's BSSID, channel and name, clears the list,
and puts the radio into promiscuous mode on that channel. For each data frame,
whichever of the two addresses is the AP, the other end is its client
(`staPickClient`). Broadcast and multicast addresses are dropped, since a
client is always unicast. A client already in the list is refreshed rather than
added again, so a chatty device is one row, not a flood. The list is capped at
`STA_MAX` (32).

Both ways out drop promiscuous mode and hand the radio back (`WiFi.mode(STA)`):
**Back** returns to the AP detail, **Exit** leaves the feature. Leaving the
radio in promiscuous would make the next feature open onto a stale channel and
look broken.

## Deauth handoff

Tap a client row to select it (it gets a `>` and turns orange), then press
**Deauth** (the up slot) to kick it off the AP. That opens a deauth view
showing the client, the AP and a running frame count; **Stop** returns to the
client list and resumes the sniff, **Exit** leaves the feature.

The deauth cannot run while sniffing: the sniff is promiscuous and the deauth
transmits, so Deauth drops the sniff, switches the radio to AP mode (the raw-TX
helper sends on the AP interface), and sends targeted frames on the AP's
channel. Each round sends two: AP to client (the client is told the AP dropped
it) and client to AP (the AP is told the client left). The receiver address is
the one selected client, not broadcast, so it is a handoff rather than the
broadcast deauther. Both ways out (Stop, Exit) hand the radio back to STA.

It reuses the WiFi Deauther's raw-frame transmitter
(`Deauther::wsl_bypasser_send_raw_frame`) and the same 26-byte frame layout, so
there is one transmit path, not two.

## What a check holds

`tools/check_station_scanner.py` pins the five ways this has a failure mode:

- the callback takes only data frames, and only after a length check (reading
  the second address off a short frame is a buffer overrun),
- broadcast and multicast are not listed as clients,
- the pick rule returns the non-AP end of the frame, not the AP,
- a client seen twice is one row, not two,
- both exits drop promiscuous mode.

Each was broken on purpose to confirm the check fails, per `CONTRIBUTING.md`.
