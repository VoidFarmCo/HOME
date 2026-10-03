# Deauth All (WiFi Deauther)

The WiFi Deauther normally hits the one AP you selected (it broadcasts deauth to
that AP's clients). **Deauth All** cycles every AP in the scan instead, each on
its own channel, the way Marauder's "deauth all" mode does.

It is a mode of the existing Deauther, toggled on the attack screen, so it reuses
the scan, the frame builder and the transmit path.

## Using it

1. Open **WiFi, WiFi Deauther**, let it scan, tap any AP to reach the attack
   screen.
2. Press **All** (the up slot) to switch the target to every scanned AP; the
   header reads `Target: ALL (N APs)`. Press **One** to go back to the single AP.
3. Press **Start**. While running it rotates through the AP list, sending a deauth
   for each on that AP's channel. **Stop** halts it, **Back** returns to the list.

It opens on single-AP targeting every time (`deautherSetup` resets the flag), so
all-APs mode is never a surprise.

## How it works

`s_deauthAll` selects the mode. In the send tick (every 100 ms), with the flag
set it advances `s_deauthAllIdx = (s_deauthAllIdx + 1) % network_count` and sends
`wsl_bypasser_send_deauth_frame(&ap_list[idx], ap_list[idx].primary)`, on the AP's
own channel, so each frame lands on the right one. With the flag clear it sends to
`selectedAp` on `selectedChannel`, exactly as before.

## What a check holds

`tools/check_deauth_all.py` (scoped to the Deauther namespace, since
ProbeRequestFlood has a near-identical attack screen) pins: the send cycles the
whole list on each AP's channel, it is guarded by the flag, the Up slot toggles
it, and `deautherSetup` resets it off. Each was broken on purpose to confirm the
check fails.
