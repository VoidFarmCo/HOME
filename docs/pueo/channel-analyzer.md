# Channel analyzer (Packet Monitor sweep)

Packet Monitor watches one channel and draws the packet rate as a waterfall.
The **Sweep** slot on the nav bar turns it into a 2.4 GHz occupancy meter: the
radio hops across all channels and each one gets a bar showing how busy it was
when the radio last sat on it. A glance says which channel is congested, which
is what you want before you pick one to work on or to park an AP on.

It is a mode of Packet Monitor, not a separate menu entry. The WiFi grid is
full at twelve tiles (see `check_grid_capacity.py`), and a channel analyzer is
the same radio in the same promiscuous mode as the waterfall, so it belongs
behind the same tile rather than next to it.

## Using it

1. Open **WiFi, Packet Monitor**. It starts on the single-channel waterfall,
   exactly as before.
2. Press **Sweep** (the up slot on the nav bar). The body switches to a bar
   per channel, 1 on the left to 14 on the right.
3. The channel the radio is dwelling on is drawn in the accent colour, its
   number too. It moves left to right and wraps.
4. Press **Sweep** again to go back to the waterfall, or **Exit** to leave.

The bars are scaled to the busiest channel, so the tallest bar is always full
height and the rest are relative to it. An empty band shows flat: nothing is
the honest reading, not a fault.

## How it works

The sweep hops on a fixed dwell (`SWEEP_DWELL_MS`, 150 ms) through 1 to
`MAX_CH`, wrapping with `(s_sweepCh % MAX_CH) + 1`. Each loop folds the frames
the promiscuous callback counted into the dwelling channel's bucket
(`s_chanPkts[s_sweepCh]`), and the hop clears the incoming channel's bucket so
a bar measures one dwell rather than climbing forever. The same callback,
channel setter and exit path the waterfall uses are reused unchanged; the only
new state is the mode flag, the dwell clock and the per-channel counts.

150 ms per channel is a full pass of fourteen channels in just over two
seconds. Shorter dwells miss bursty traffic between beacons; longer ones make
the pass feel slow to read. It is a constant at the top of the namespace, not
a guess buried in the loop, so it is one line to change.

## What a check holds

`tools/check_channel_sweep.py` pins the four ways this has a failure mode:

- the bar scaling guards against an all-quiet band (divide by zero),
- the hop visits every channel 1 to `MAX_CH` and never 0 or `MAX_CH`+1,
- the hop clears the new channel's count so bars do not accumulate forever,
- the mode is off on entry, so a re-entry after a sweep opens on the waterfall.

Each one was broken on purpose to confirm the check fails, which is the rule
in `CONTRIBUTING.md`: a check that has never failed has not been tested.
