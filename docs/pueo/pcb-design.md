# Pueo carrier board — design notes

Input for a KiCad project, not a layout. Everything here is derived from the
firmware pin map (`ESP32-DIV/board_pueo.h`), the enclosure
(`docs/pueo/pueo-enclosure.scad`), and module datasheets. Figures that need checking
against a meter are marked **[verify]**.

## What this board is

The CYD lives in the **lid** — the enclosure cuts a 55.5 × 92 window for its
bezel — and everything else sits in the 85 × 170 × 20 base. So a harness
between the two halves already exists in the hand-wired build. This board
replaces that harness and the loose modules with one PCB in the base, joined
to the CYD by a single connector.

It is explicitly **not** a shield. It cannot be, and that is worth
understanding before anything is drawn.

## Why it cannot plug in

**This board is drawn for the 3.5" ESP32-3248S035R**, which is the only panel
the firmware supports. It was drawn for the 2.8" ESP32-2432S028R until that
panel was dropped after 0.4.13, and retargeting it cost three rows of J1 and
nothing else: the outline comes from the enclosure base rather than from the
display, and the base does not change with the panel.

The three are `CC1101_CS`, GPIO 21 rather than 27 and on a header rather than
a pad; `NRF24 CSN`, GPIO 25 rather than 4; and the serial connector, which
this board calls P1 and the other called P5. The first two are in
`board_pueo.h` and always were: the backlight and the chip select swap 21 and
27 between the two boards, and 25 is free here because this panel's touch
controller hangs off the display's SPI instead of taking its own bus at
25/32/39.

Of the ten signals the firmware needs, only four reach a CYD header:

| signal | GPIO | where it is |
|---|---|---|
| CC1101 CS | 21 | P3 header |
| CC1101 GDO0 | 22 | P3 header |
| CC1101 GDO2 | 35 | P3 header |
| GPS RX | 1 | P1 JST |
| VSPI SCK | 18 | **microSD slot pin** |
| VSPI MOSI | 23 | **microSD slot pin** |
| VSPI MISO | 19 | **microSD slot pin** |
| NRF24 CSN | 25 | **module pad** |
| NRF24 CE | 16 | **RGB LED pad** |
| PN532 SS | 17 | **RGB LED pad** |

The SPI bus itself is not exposed. Six of ten signals require soldering to
pads or module pins on the CYD, so any carrier board needs a short pigtail
from those pads to a connector. The gain is that it becomes **six joints in
one documented place** instead of thirty scattered across four modules — and
an intermittent joint on MISO presents exactly like the bus faults that cost
a week of firmware archaeology, so reducing their count is not cosmetic.

**[verify]** Confirm the P3 and P1 pinouts against your actual board before
committing. CYD revisions differ, and the silkscreen is the authority.

## Architecture

```
  LID                              BASE (this board)
  ┌──────────────┐                 ┌────────────────────────────┐
  │ CYD          │                 │  radios · power · SMAs     │
  │  headers ────┼──── harness ────┤  J1  14-way                │
  │  pigtail ────┤                 │                            │
  └──────────────┘                 └────────────────────────────┘
```

One connector, J1.

**[decide] which part.** This said 14-way 1.25 mm JST-GH, on the grounds that
GH is easier to hand-assemble than FFC and tolerates a lid opening and
closing. The second half of that still holds. The first half does not, and it
was written without checking what a 14-way GH actually costs to obtain.

`GHR-14V-S` is a catalogue part and distributors stock it. The hobby market
does not: GH is stocked in 4, 6, 8 and 10 because those are the Pixhawk sizes,
so a search turns up nothing at 14. That leaves buying a bare housing and a
bag of `SSHL-002T-P0.2` contacts and hand-crimping fourteen of them at
0.2 mm², which is the opposite of easier to hand-assemble. A crimp that grips
insulation rather than conductor reads fine on a meter and fails under load,
and fourteen chances at it on the one cable carrying the whole SPI bus is a
poor trade for a connector choice nobody is committed to.

Four ways out, none of them decided:

| option | for | against |
|---|---|---|
| **GH 14-way** | one connector, good flex life, the pinout as drawn | hand-crimp only, awkward to source |
| **GH 8 + 6** | both stock sizes, available pre-crimped | splits a multi-drop bus across two housings, and the grounds on 1, 2 and 14 were placed to bracket it |
| **0.5 mm FFC** | any way count, trivially sourced, thinnest | flex life on a hinge is the original objection and still stands |
| **PicoBlade 14-way** (`51021-1400`) | same 1.25 mm pitch, better availability at odd sizes | latches on top rather than the side, so it flexes differently from GH |

The deciding evidence is how the lid actually moves in the printed case, which
is a thing to handle rather than to reason about. Until then this is open, and
the pinout below is independent of it: whichever part wins, the fourteen nets
and their order do not change.

**Whatever it is, the wire is 28 AWG.** GH contacts take about 32 to 28 and
are rated 1 A each, and 28 is what pre-crimped GH cable comes as. The same
figure applies to PicoBlade. So a contact is the limit here, not the copper.

**[decide] whether one 5 V pin is enough**, and this depends on the power tree
above rather than on the connector. J1 has three grounds and one supply: GND on
1, 2 and 14, `+5V_SW` on 3. The return is split three ways and the feed is not.

At 1 A that is comfortable while the carrier feeds the CYD, which draws around
200 mA rising to 500 on WiFi transmit. It is not comfortable the other way
round. If the CYD ends up owning the battery and feeding the carrier from P1,
pin 3 carries three radios, the PN532 and the 3.3 V buck through one contact,
and that wants a second 5 V pin paralleled the way the grounds already are.
Deciding the power tree first and the pin count second keeps that from being
discovered after fabrication.

### J1 pinout

| pin | net | to CYD | note |
|---|---|---|---|
| 1 | GND | GND | |
| 2 | GND | GND | two grounds, one at each end of the connector |
| 3 | +5V_SW | 5V in | board feeds the CYD, see power tree |
| 4 | VSPI_SCK | GPIO 18 | SD slot pad |
| 5 | VSPI_MOSI | GPIO 23 | SD slot pad |
| 6 | VSPI_MISO | GPIO 19 | SD slot pad |
| 7 | CC1101_CS | GPIO 21 | P3 |
| 8 | CC1101_GDO0 | GPIO 22 | P3 |
| 9 | CC1101_GDO2 | GPIO 35 | P3, input-only on the ESP32 side |
| 10 | NRF_CSN | GPIO 25 | module pad, not a header |
| 11 | NRF_CE | GPIO 16 | LED pad |
| 12 | PN532_SS | GPIO 17 | LED pad |
| 13 | GPS_TX | GPIO 1 | **through R1, see below** |
| 14 | GND | GND | |

All three CC1101 control lines land on P3, which is the one place this board
is easier than its predecessor: chip select, GDO0 and GDO2 reach a connector
rather than costing a joint.

Ground on pins 1, 2 and 14 so every signal has a return nearby. On a
multi-drop SPI bus run through a cable this matters more than the pin count
suggests.

## The GPS series resistor

R1, 1 kΩ, in the GPS TX line before J1 pin 13.

GPIO 1 is UART0's transmit pin. The firmware releases it before the GPS
feature reads on it (`gpsPortOpen()` in `gps.cpp`), but the console owns it
the rest of the time, which means the ESP32 and the GPS are both driving that
net whenever a GPS feature is *not* open. R1 limits the contention current.

This is the single change most worth having in copper rather than in a
build note, because it is invisible, it is easy to forget, and forgetting it
degrades a pin slowly rather than failing loudly.

### The GPS is a 3.3 V part, and it feeds its own antenna

From the ATGM336H-5N user manual, v1.2, which settles what was a [verify]:

| | |
|---|---|
| supply | 2.7 / **3.3** / 3.6 V |
| **absolute maximum** | **3.6 V** |
| typical | <25 mA at 3.3 V |
| peak, excluding antenna | **100 mA** |
| backup, VBAT | 1.5 to 3.6 V, 10 uA |

**5 V would destroy it.** 3.6 V is the absolute maximum on VCC, not a
recommended ceiling, so this part belongs on +3V3_RF and nowhere else. It is
on that rail in the tree below, which is correct, and the peak figure is four
times what the estimate said.

**The active antenna is not a separate supply problem.** Pin 14 VCC_RF is an
*output*, +3.3 V, and the module biases the antenna from it through an
inductor -- the datasheet's own application circuit does exactly that, with
detection and short-circuit protection built in. Budget for it on +3V3_RF
rather than anywhere else: 3 mA with the antenna open, 50 mA into a short,
which the module limits rather than passing through.

**[verify] the backup cell.** VBAT keeps the RTC and SRAM alive for 10 uA and
is what separates a 1 s hot start from a 35 s cold one. Nothing in this design
says where it comes from, and the breakout in hand has not been looked at. A
GPS that cold-starts every time is a usability problem rather than a fault, so
it would be easy to ship without noticing.

## Power tree

**The carrier makes its own 5 V, and that is settled.** This was briefly open,
because the CYD carries an FM5324GA that is a charger and a 5 V boost in one
package and it looked as though the carrier could simply take 5 V from P1 and
delete its own TP4056 and MT3608.

It cannot. Measured on 2026-09-26: P1's `5V` sits at about 4 V unloaded and at
**0 V under 92 mA**, on a 3.8 V cell. It is a high-impedance node, not a
source, and Sunton labels P1 the "4P 1.25 Power supply base" because power is
meant to go in there rather than come out. See hardware.md.

So the tree below stands as drawn. One thing follows from it that did not
before:

**[verify] before J1.3 is routed to P1 at all.** The pin is an input, so
feeding the carrier's 5 V into it is the direction it was designed for. What
has not been measured is what sits between that pin and the cell: whether the
charger back-feeds, and whether the pin is live when USB is attached. Powering
the board through P1 while USB is also plugged in puts two supplies on one
node, and nothing here knows yet what arbitrates them.

```
  USB-C ──► TP4056 ──► 1S LiPo ──► MT3608 ──► +5V_SW ─┬─► CYD (J1.3)
           (1 A chg)   3.0-4.2 V     boost             ├─► PN532 VCC
                                                       │
                                                       └─► MP2307 ──► +3V3_RF ─┬─► NRF24
                                                           buck                ├─► CC1101
                                                                               └─► ATGM336H
```

**The buck has to hang off the boost, not off the battery.** An earlier
draft of this tree fed it from +VSYS directly, which cannot work: a buck
only steps down, and 1S LiPo swings 3.0-4.2 V against a 3.3 V target. Below
about 3.6 V the rail would sag with the battery and the PA modules would
brown out exactly when the pack is low. The MP2307 module is spec'd from
4.75 V input in any case, so it is out of range across the whole discharge
curve. Boost to 5 V, then buck to 3.3 V.

That costs a conversion: two switchers in series at roughly 90% each is
about 81% end to end on the RF rail, against the ~90% a single buck-boost
would manage. A proper buck-boost is a real option if efficiency matters
more than using the part already on hand.

The separate 3.3 V rail itself is not optional. The PA/LNA modules brown out
if they share the CYD's regulator, which is why the enclosure already
allocates a third power module at (-29, -48).

### Budget

| load | typical | peak | source |
|---|---|---|---|
| load | rail | typical | peak | source |
|---|---|---|---|---|
| CYD (ESP32 + ILI9341 + backlight) | +5V_SW | ~200 mA | ~500 mA on WiFi TX | **[verify]** |
| PN532 | +5V_SW | ~10 mA idle | ~100 mA field on | **[verify]** |
| NRF24L01+PA+LNA | +3V3_RF | 45 mA RX | ~115 mA TX @ +20 dBm | **[verify]** |
| CC1101 | +3V3_RF | 16 mA RX | ~34 mA TX @ +10 dBm | **[verify]** |
| ATGM336H | +3V3_RF | <25 mA @3.3 V | **100 mA peak** | datasheet |

With the buck downstream of the boost, the 3.3 V loads no longer draw from
the battery in parallel with the CYD — they are reflected onto +5V_SW,
scaled by 3.3/5 and divided by the buck's efficiency:

```
                  CYD   PN532   +3V3_RF        +5V_SW    battery @ 3.7 V
  worst case      500     100   189 -> 139     739 mA         1174 mA
  realistic       500      10   155 -> 114     624 mA          992 mA
```

Worst case assumes everything transmits at once, which the SPI bus makes
impossible: it is shared and `SpiBus` enforces one owner at a time, so the
radios cannot all be mid-transaction together. Realistic is CYD on WiFi plus
one radio plus GPS.

**Size the boost for 800 mA continuous at 5 V**, which is about **1.2 A from
the cell** at peak. That is up from the earlier figure, and the increase is
the direct cost of moving the buck downstream — the RF rail used to bypass
the boost entirely. The cell and its protection circuit must tolerate 1.2 A;
many small protection boards cut out around 2 A, which is closer than it
sounds. The MT3608 is rated 2 A switch current, so it is inside spec but
will run warm in a sealed enclosure.

**Runtime**, 2000 mAh cell, 600 mA average at 5 V: roughly **2 hours**.
If that is short, the lever is the backlight, not the radios.

### Decoupling

- 10 µF ceramic across the NRF24 supply **at the module pins**, not near the
  regulator. This module is notorious for browning out on TX transients and
  the inductance of a few centimetres of trace is enough to cause it.
- 100 nF at every module's VCC pin.
- 100 µF bulk on +3V3_RF at the buck output.
- 220 µF bulk on +5V_SW at the boost output, close to J1.

### Trace widths

1 oz copper, outer layer, 10 °C rise:

| net | current | width |
|---|---|---|
| battery, boost in/out, +5V_SW | up to 1.6 A | **1.0 mm** |
| +3V3_RF | up to 500 mA | **0.5 mm** |
| signals | — | 0.25 mm |

## SPI signal integrity

Five devices on one bus, three of them through a cable, is the part of this
design most likely to disappoint.

- Keep module stubs off the SCK/MOSI spine as short as the placement allows.
  Daisy-chain along the spine rather than star-wiring from J1.
- Footprints for **22 Ω series resistors on SCK and MOSI** at the J1 end.
  Fit 0 Ω initially; they are there so ringing is a part swap, not a respin.
- The CC1101 runs at 4 MHz and the NRF24 at up to 10 MHz, both set in the
  `kProfiles` table in `ESP32-DIV/SpiBus.cpp`. At 10 MHz through a cable, edge
  rates matter.
- Chip selects are the safe nets to route awkwardly. They are static during a
  transaction, so a long CS trace costs nothing.

## Mechanical

Base interior is 85 - 2×2.5 = **80 mm** wide and 170 - 5 = **165 mm** long,
with a 2 mm pocket and 4 mm floor. Mounting bosses are M3 self-tap pilots,
R4.0, inset 6 mm from the corners.

Module positions are already fixed by the enclosure (origin = case centre):

| module | centre (x, y) | size |
|---|---|---|
| MT3608 boost | -19, -65 | 36 × 17 |
| TP4056 charger | 26.6, -62 | 26 × 19 |
| MP2307 buck | -29, -48 | 17.9 × 12 |
| LiPo pack | -16, -23 | 45 × 34 |
| ATGM336H GPS | 0, 52 | 16 × 13 |
| PN532 V3 | 0, 18 | 43 × 41 |
| CC1101 HW-863 | -23, 61.5 | 15 × 40 |
| NRF24 PA+LNA | 22, 61 | 16 × 41 |

Constraints that follow:

- **SMA bulkheads at x = -23 and x = +22**, 45 mm apart, axis 5 mm above the
  pocket floor, through the top wall. The radio footprints must put their
  board SMAs exactly there.
- **PN532 needs a 1.4 mm floor window** under its coil at (0, 18). Keep
  copper — especially ground pour — out of that footprint's area on both
  layers, or the field is attenuated by your own board. Now confirmed at the full
  43 x 41 extent, so the keepout and the floor window are both correctly
  sized -- the module is exactly as large as the design assumed.
- **USB-C on the right wall** at y = −62, 20 × 7 cutout.
- **GPS antenna** is a separate 20 x 6 mm board on a 90 mm u.FL pigtail, not
  a patch on the module. It needs a flat spot with sky view and a thinned
  wall, but it is no longer tied to where the module sits -- and the 90 mm
  is a hard limit. See the GPS section below; the top-centre slot the
  enclosure cuts today is out of reach from the module's current position.
- Battery pocket at (−16, -23) is 45 × 34 and must stay clear of copper.

### Recommendation: keep the radios as modules

Header sockets for the HW-863, NRF24 and PN532 rather than integrating the
silicon. Integrating CC1101 or NRF24 means matching networks, antenna tuning
and two or three spins before it works as well as a $4 module. Modules also
let you swap a dead PA board in the field. The cost is height, which this
enclosure has.

## Generated inputs

`tools/gen_netlist.py` emits into `dist/pcb/`:

```
netlist.txt        17 nets, 73 connections, net by net
pueo-carrier.net   KiCad legacy netlist
placement.csv      module centres in board coordinates
bom.csv            25 parts
```

Pins are named functionally (VCC, SCK, CSN), not numbered. Module pin numbers
come from datasheets, and none of these modules has a standard library
footprint, so numbering them here would be inventing data. The netlist gives
the connections; the datasheets give the numbers.

The generator refuses to write anything that disagrees with
`board_pueo.h`, so the netlist cannot drift from the firmware. It also
rejects single-ended nets, which caught a real mistake: an initial `USB_VBUS`
net had one endpoint, because the Type-C jack is on the TP4056 module rather
than on this board.

Enclosure coordinates convert to a top-left origin with Y downward:

```
kicad_x = enclosure_x + 40      board is 80 x 165 mm
kicad_y = 82.5 - enclosure_y    the base interior, less 2.5 mm walls
```

Checked after generation: all eight modules sit inside the outline, none
overlap, and the SMA positions land exactly on the J2 and J3 centres. The
tightest gaps are 2 mm (PN532 to NRF24, and the buck to the battery), so
there is little room to move anything without revisiting the enclosure.

**The KiCad `.net` file has never been opened in KiCad** — it is not installed
on the machine that generated it. `netlist.txt` is the deliverable; the
`.net` is a convenience that may need hand-fixing.

## What the first datasheet changed

The TP4056 module, photographed and measured, disagreed with three
assumptions. Two were paperwork. One was an electrical fault.

**It is micro-USB, not USB-C.** Both the BOM and the enclosure said Type-C.

**It is 27 x 17 mm, not 26 x 19.** The enclosure pocket is wrong in both
axes, and it mounts rotated 180 degrees from how it is usually photographed
so the jack faces +X and reaches the right wall.

**The load was wired around the protection circuit.** This is the one that
mattered. On these boards the DW01/FS8205 protection MOSFETs sit in the
*negative* line, between `B-` and `OUT-`:

```
  BT1 + ────────────► B+            OUT+ ────► U1.VIN, U3.VIN
  BT1 - ────────────► B-   [FETs]   OUT- ────► system ground
```

`B-` and `OUT-` are not the same node. The first netlist put the battery
negative on the common ground net and took the converters off `B+`, which
runs the entire load around the protection. Charging would have worked,
over-current would have worked, and **over-discharge cutoff would silently
not exist** — the failure mode being a flat lithium cell taken below 2.5 V
because nothing was watching.

Now `VBAT`/`BATT_NEG` reach only the battery and the module, and everything
else hangs off `+VSYS` and a ground that is `OUT-`.

Worth generalising: the enclosure was laid out from module outlines, and an
outline tells you nothing about which terminal is which. Expect the other
datasheets to move things too.

## The MT3608, and a bring-up order that matters

Pin names are `VIN+`/`VIN-`/`OUT+`/`OUT-`, not the `VIN`/`VOUT`/`GND` the
netlist first assumed. `VIN-` and `OUT-` are the same node on a boost, so
both land on ground.

Two things about this module are worth more than a pin correction.

**Both switchers ship adjustable, and neither ships at the voltage you
want.** The MT3608's is a 25-turn pot; the MP2307's is a single-turn SMD
trimmer, which is worse to set precisely because the whole range is in one
rotation. Set *both* against a meter, into no load, before either one is
connected to anything. The buck is the dangerous one: +3V3_RF feeds the
NRF24, the CC1101 and the GPS directly, with no regulator downstream to
absorb a mistake.

**The output is a multi-turn trimpot, not a fixed 5 V.** These ship at an
arbitrary setting and the MT3608 will happily produce about 28 V. Connecting
J1 to an unadjusted module destroys the CYD, and the ESP32 behind it,
instantly and permanently.

So the assembly order is not a preference:

```
1.  Power the boost from the battery with NOTHING connected to OUT+
2.  Meter OUT+ to ground, turn the trimpot to 5.00 V
3.  Only then fit J1
```

TP1 exists on `+5V_SW` for exactly this. Worth a line of silkscreen next to
the pot saying `SET 5V FIRST`, because the person who assembles the second
one in a year will not remember.

**It has its own micro-USB jack**, which this design does not use — the boost
is fed from `+VSYS`. The enclosure cuts only one USB opening, at the TP4056,
so the boost's connector ends up inside the case. That is the right outcome
(two identical micro-USB sockets, one charging and one backfeeding the boost
input, is a support question waiting to happen) but it does occupy space and
wants clearance from anything it could short against.

The MT3608 silicon is SOT-23-6, 2.9 x 1.6 mm, 0.95 mm pitch, if integrating
it ever becomes tempting. The recommendation above still stands: the module
costs about a dollar and comes with its inductor, diode and feedback network
already laid out and working.

## Module dimensions

Collected as datasheets and listings turn up. Footprints are mostly settled;
heights are the gap, and heights are what the enclosure budget runs on.

```
module           footprint mm    height mm    basis
--------------   -------------   ----------   -----------------------------
MT3608 boost     36 x 17  (?)    6.25 or 14   CONFLICT, and it now decides
                 30 x 17  (?)                 whether the module fits at all
TP4056 charger   27 x 17         ~4  (est)    micro-USB jack is the tallest
                                              thing on it, ~2.7 over ~1.0
MP2307 buck      17.9 x 12       ~6  (est)    inductor and trimmer stand
                                              proud of a ~1.0 board
PN532 V3         43 x 41         ~3.5 (est)   flat board, no tall parts
ATGM336H GPS     16 x 13         ~3.5 (est)   shield can over a ~1.0 board
HW-863 CC1101    28 x 15         ~7  (est)    set by the SMA barrel, 6.35
                                              OD, axis near the board top
NRF24 PA+LNA     41 x 15.5       ~8  (est)    SMA barrel plus the shield can
LiPo pack        45 x 34         ?            depends on the cell; 6-10 for
                                              a 2000 mAh pouch
```

**The five marked `(est)` are reasoned from the tallest visible component,
not measured.** Listings for these parts publish footprint and almost never
publish height -- I went looking and it is simply not there. They are good
enough to answer the question below, and not good enough to cut plastic to.

One number got confirmed rather than estimated: an SMA coupling barrel is
6.35 mm outside diameter, fixed by the connector standard rather than by a
vendor. `SMA_D = 6.5` in the enclosure is a 0.15 mm clearance hole on that,
so the bulkhead holes have been right all along.

Both radios land within a millimetre of what the enclosure already guessed:
CC1101 at 15 wide against a 15 mm pocket, NRF24 at 15.5 against 16. Their
pocket lengths (40 and 41) hold too, and the CC1101's 38 mm overall leaves
2 mm. Nothing moves horizontally for either.

The CC1101 drawing confirms something the enclosure was already built
around: **the SMA jack is soldered to the module**, edge-mounted, with the
2x4 pin header at the opposite end. The comment in the enclosure --
"radios: vertical, board SMA against the top wall" -- had this right. It
means the radio SMAs are not free to move: they sit wherever the module
sits, which is the constraint the next section runs into.

## Height, and the pockets problem

**The MT3608 has two contradictory dimension sets and they disagree in the
axis that matters.**

```
  36 x 17 x 6.25 mm     with a 25-turn trimpot
  30 x 17 x 14 mm       Amazon listing, "Board Size (L*W*H)"
```

Width agrees. Length differs by 6 mm, height by more than double. The
likely reading is that 6.25 mm is the bare PCB plus low components while
14 mm is the overall height with the trimpot and inductor standing proud --
the blue 3296-style multi-turn pot is tall on its own -- and that 30 vs 36
is a variant difference. But that is a guess, and the conclusion flips on
it, so it wants a caliper rather than a listing.

The height is the first real figure in a budget that has been running on
assumption, and it surfaces something structural either way.

Vertical space in the base:

```
  base outer height              20.00 mm
  floor less pocket depth         2.00 mm
  pocket floor to base top       18.00 mm    everything lives in here
```

### How much room is there, really

The carrier does not sit on the pocket floor. It sits on standoffs, because
through-hole leads have to go somewhere. That was missing from the earlier
arithmetic, and it changes the answer:

```
  cavity above the pocket floor        18.00 mm
  standoff 2.5 + carrier 1.6            4.10 mm
  room above the carrier               13.90 mm
```

**13.90 mm is the budget every soldered-down module lives inside.** Which
makes the MT3608 conflict decisive rather than academic:

```
  MT3608 at  6.25 mm   ->  +7.65 mm spare
  MT3608 at 14.00 mm   ->  -0.10 mm
```

At 14 mm it does not fit. Not "fits tightly" -- over by a tenth of a
millimetre, before any tolerance on the standoff or the print.

This corrects what this document said one revision ago, which was that at
14 mm the module "fits, 1.65 mm spare" soldered direct. That was carrier
plus module with the standoff left out. With the standoff counted the two
candidate dimensions give opposite answers to "does this design work", so
the MT3608 measurement is no longer one detail among several.

### The PN532 pays for height twice

Copper keepout solves one half of the NFC problem. The other half is
distance, and a carrier board makes it worse rather than better.

The base thins its floor to 1.4 mm under the coil specifically so the field
can reach through the case. That budget assumed the module sitting on the
pocket floor. Put it on a carrier instead and the coil moves up by the board
plus whatever holds it:

```
  soldered direct              1.6 mm further from the outside surface
  low-profile socket 5.0       6.6 mm
  standard socket 8.5         10.1 mm
```

Read range on a PN532 is a couple of centimetres to begin with. Ten
millimetres of added standoff is a large fraction of it, and it is spent on
nothing the user gets back.

## The GPS is a different module than assumed

The part in hand is an **ATGM336H** on a GOOUUU breakout, 16 x 13 mm, with a
1x5 header silkscreened VCC / GND / TX / RX / PPS. Two things follow.

**The footprint collapses.** 28 x 27 was budgeted; 16 x 13 is what turned
up. That is roughly 550 mm2 of floor handed back, in the middle of the
board, next to the battery. It also resolves the `[verify pinout]` note on
J5 -- the silkscreen matches the assumed order exactly, so the netlist
stands.

Firmware is unaffected: `GPS_UART_BAUD` is 9600 and the ATGM336H defaults to
9600 NMEA, same as the GT-U7 would have.

**The antenna moves off the module**, onto a 20 x 6 mm board on a 90 mm u.FL
pigtail. This is mostly good -- a patch soldered to the module has to sit
wherever the module sits, and this one does not. But 90 mm is short, and it
is measured through whatever path the cable can actually take:

```
  from J5 at (24, -21) to ...          straight line
  top centre, the slot cut today          94.1 mm    over
  top left, clear of the NRF24           105.8 mm    over
  upper right wall                        61.5 mm    ok
```

Straight-line is the optimistic case; the cable has to route around modules,
so treat anything past about 75 mm as doubtful.

### Where it went

**J5 moved from (24, -21) to (0, 52).** The antenna slot the enclosure
already cuts sits hard against the inside of the top wall, centred, so the
antenna lands at about (0, 80). From the new position the run is **28 mm**
against 90 mm of cable -- comfortable even after routing around things, with
enough left over that the excess has to be coiled somewhere.

The two radios leave a corridor between them:

```
  CC1101 right edge    x = -15.50
  NRF24  left edge     x = +14.25     29.75 mm of clear corridor
  PN532  top edge      y = +38.50
```

A 16 mm module centred in 29.75 mm has room on both sides, and y = 52 sets
the clearances evenly:

```
  to CC1101   7.50 mm
  to NRF24    6.25 mm
  to PN532    7.00 mm
```

All three beat the 2 mm that is the tightest gap elsewhere on the board.
Biasing left, toward the CC1101 and away from the 2.4 GHz PA, was the
obvious temptation -- but it buys about a millimetre of separation while
cutting the CC1101 gap to 2.5 mm, which is a bad trade. Centred is better.

This leaves 28 x 27 of floor free at the old position, next to the battery.
Nothing needs it yet.

One thing the move does not fix: the antenna still ends up between the two
SMA bulkheads, so a -130 dBm L1 receiver sits between a 433 MHz transmitter
and a 2.4 GHz PA. The pigtail means it *could* go elsewhere -- flat against
a side wall or the inside of the lid -- which is a freedom the patch-antenna
assumption never had. That is an enclosure decision rather than a board one,
and it can be made later without moving J5 again: 90 mm of cable reaches
most of the upper half of the case from (0, 52).

So the PN532 is the one module with a reason not to be socketed, which cuts
against the swappability argument that applies to the radios. Three ways
out, none free:

1.  **Solder it down** and accept 1.6 mm. Cheapest, loses the ability to
    swap a module whose counterfeit rate is not low.
2.  **Cut it out of the carrier entirely** -- leave it on the floor in its
    existing pocket and run a short flying lead to the board. Keeps the
    range, adds a cable and an assembly step.
3.  **Window the carrier** so the PN532 hangs through a cutout at floor
    level while its header still lands on the board. Best of both, and the
    most work to get right.

Option 3 is the interesting one because the board already needs a keepout
over that area -- turning a copper keepout into an actual hole costs
nothing in routing terms. It does mean the 43 x 41 region stops carrying
structure, which matters for a board that is 80 mm wide. The NRF24 with its PA/LNA can and its SMA is taller, and
the SMA axis is fixed at 5 mm above the pocket floor by the case wall. That
constraint and a carrier board are in direct tension: raising the modules by
a PCB plus a socket raises their SMAs too, and the bulkhead holes do not
move.

**Which is the real finding: the enclosure is built for modules sitting
directly on the floor.** It cuts an individual pocket per module, at
per-module depth. A carrier board is one flat plane spanning all of them, so
those pockets stop being useful and start being obstructions, and the SMA
height stops lining up.

That is not an argument against the board. It means the base needs revising
alongside it:

```
per-module pockets   ->  one flat shelf at a single height for the PCB
mounting bosses      ->  positioned for PCB holes, not module corners
SMA_Z = 5.0          ->  recomputed from PCB + socket + module-SMA height
```

Worth resolving before layout rather than after, because SMA height drives
where the radio modules sit vertically, which drives socket choice, which
drives whether the radios are swappable or soldered down.

**Current recommendation**: low-profile sockets for the radios, so a dead PA
module can still be replaced, and solder the power modules directly since
they will not be swapped. That splits the difference on height and keeps the
part that actually fails serviceable.

### Enclosure changes: applied

Done in `docs/pueo/pueo-enclosure.scad`. Both parts still render manifold.

The enclosure now lives in this repo. It was a loose file in the parent
directory, tracked by nothing, while being the thing every dimension in this
document is measured against. The working copy alongside the repo has since
been renamed to match, so both are `pueo-enclosure.scad`; the tracked one is
authoritative and the two are currently in sync.

```
MODULES[1]   26 x 19  ->  27 x 17         TP4056 pocket
MODULES[4]   28 x 27 at (24,-21)          GPS pocket shrunk and moved to
             ->  16 x 13 at (0, 52)       the corridor between the radios
GPS_D        5  ->  7                     the antenna board is 6 mm deep
                                          and would not fit a 5 mm recess
USBC_W/HT    20 x 7  ->  USB_W/HT 11 x 6  micro-USB, not Type-C; clears the
                                          plug shell, not the overmould
SMA_Z        5.0  ->  derived             now a formula, see below
NFC_INDEX    5  ->  looked up by name     reordering MODULES can no longer
                                          point the thin floor at the wrong part
```

Housekeeping picked up along the way: the header said `85 x 140 x 20` while
`L = 170`, and it still carried the name the workspace had before this was
called Pueo. Both fixed. Part names in MODULES
now match the netlist (`ATGM336H GPS`, `MP2307 buck`).

Pockets left deliberately oversized, with a comment saying so: MT3608 36 x 17
for a part that may be 30 mm, CC1101 15 x 40 for a 15 x 38, NRF24 16 x 41 for
a 15.5 x 41, buck 20 x 12 for a 17.9 x 12. `pockets()` adds a further 0.6 mm
to each dimension, which is worth knowing -- the NRF24's length clearance is
0.6 mm rather than the zero the nominal numbers imply. Still tight for FDM,
so still worth a test print, but not the interference it looked like.

### SMA_Z is now derived, and it is not 5.0

The old value was asserted. It cannot be: both radios carry their own
edge-mounted SMA, so the bulkhead height is wherever the module's connector
ends up once the module is stacked on a carrier board.

```
SMA_Z = STANDOFF_H + PCB_T + SOCKET_H + MOD_PCB_T + SMA_AXIS_H
```

`MOD_PCB_T` (1.0) and `SMA_AXIS_H` (2.5) are marked `[VERIFY]` -- they need
the radio modules in hand. With the current values and the radios soldered
down, OpenSCAD echoes **SMA_Z = 7.6 mm**, against the 5.0 the case was cut
for. The bulkheads move up 2.6 mm.

### Only two unknown heights can change anything

Seven heights were open. Sorting them against the 13.90 mm budget collapses
the list:

```
  MT3608 boost     6.25 or 14      DECIDES IT -- 14 does not fit
  LiPo pack        6-10            structural, not a fit question
  NRF24 PA+LNA     ~8   est        ~6 mm clear at the estimate
  HW-863 CC1101    ~7   est        ~7 mm clear
  MP2307 buck      ~6   est        ~8 mm clear
  TP4056 charger   ~4   est        ~10 mm clear
  ATGM336H GPS     ~3.5 est        ~10 mm clear
  PN532 V3         ~3.5 est        sits on the floor, not on the board
```

Every estimate would have to be wrong by 6 mm or more to matter, and these
are flat modules whose tallest part is visible in a photograph. Not worth
chasing further.

The radios deserve one extra note, because their SMA has to line up with a
hole in a wall. The axis must land between 3.25 and 14.75 mm above the
pocket floor for the 6.5 mm hole to stay inside the cavity, and with the
carrier top at 4.10 mm that means:

```
  radios soldered down      SMA axis 0.00 .. 10.65 mm above their own board
  radios on a 5 mm socket   SMA axis 0.00 ..  5.65 mm
```

Any real SMA mounting is 1 to 4 mm above the board it sits on. So SMA height
is not a fit risk in either configuration. It only sets where the hole goes,
and that is already a formula in the enclosure rather than a constant.

**So two measurements are blocking, and neither can be looked up:**

1.  **MT3608 overall height**, trimmer and inductor included. Over 13.90 mm
    and it cannot be soldered to the carrier at all -- which means a shorter
    boost module, a thinner standoff, or a taller base.
2.  **Battery thickness.** Not a fit question but a structural one; see
    below.

Everything else is decided.

### Still open: the carrier cannot be a flat plane

The remaining delta was "per-module pockets -> flat PCB shelf". It did not
get applied, because working through it surfaced a problem that a shelf
parameter does not solve.

The interior is 80 x 165, which is exactly the carrier outline. So a
full-span board covers the battery pocket at (-16, -23) completely. The
battery is a physical object with a thickness nobody has measured yet -- a
2000 mAh 1S pouch is typically 6 to 10 mm -- and it has to be either under
the board, on top of it, or through it.

Under it means the standoff grows to the battery's thickness, and everything
above rises with it:

```
  standoff   socket   SMA_Z    hole spans      18 mm cavity
     2.5       0       7.6     4.35 .. 10.85   ok     through-hole leads only
     2.5       5.0    12.6     9.35 .. 15.85   ok
     7.0       0      12.1     8.85 .. 15.35   ok     6 mm battery underneath
     7.0       5.0    17.1    13.85 .. 20.35   over
    11.0       0      16.1    12.85 .. 19.35   over   10 mm battery underneath
```

A 6 mm cell under the board works only with the radios soldered down. A
10 mm cell does not work at all -- the SMA holes run out through the top of
the base.

Which is the same shape of problem as the PN532: two things want to be at
floor level and the board is in the way. One answer covers both. **The
carrier wants to be a frame, not a plane** -- cut out the battery footprint
and the PN532 footprint, let both sit on the floor where the case already
has pockets and a thinned NFC window for them, and keep the standoff at the
2.5 mm that through-hole leads need. That holds SMA_Z at 7.6 and keeps NFC
range.

The cost is structural: two large holes, 45 x 34 and 43 x 41, in an 80 mm
wide board. Whether what is left is stiff enough to carry a screwed-down
lid is a question for the layout, not for the enclosure.

This needs a decision and a measured battery before the base geometry
changes. Until then the per-module pockets stay, which is the conservative
state -- they are correct if the modules sit on the floor, and harmless
extra clearance if they do not.

MT3608 keeps its 36 x 17 pocket: if the board is really 30 mm the pocket is
6 mm oversized, which is slack rather than interference, and oversizing is
the safe direction to be wrong in until it can be measured.

Both radio pockets stand as cut. CC1101 15 x 40 against a 15 x 38 module,
NRF24 16 x 41 against 15.5 x 41 -- tight on the NRF24's length with no
slack at all, so that one is worth a test print before committing.

### The NRF24 fit test

`docs/pueo/nrf24-fit-test.scad` is that test print. It renders two parts.

`PART="ladder"` is a 100.5 x 55 x 4 plate carrying five pockets at 0.2,
0.4, 0.6, 0.8 and 1.0 mm of total clearance over a 15.5 x 41 module, each
labelled and each with a window through the floor to push the module back
out. The smallest pocket the module seats into flat, without forcing, is
what this printer needs. Print this one first; it is flat and quick and it
answers the question on its own.

For reference, the pocket as currently cut is 16.6 x 41.6 -- `pockets()`
adds 0.6 to the 16 x 41 in `MODULES`. Against a 15.5 x 41 module that is
1.1 mm of clearance across the width and 0.6 along the length, so the
ladder's 0.6 rung is the closest thing to the shipping geometry. If even
the 1.0 rung is tight, the `MODULES` entry has to grow rather than the
kerf.

`PART="insitu"` is a 36.5 x 51 x 15 slice of the base around the pocket,
taken as an `intersection()` with `base()` itself rather than redrawn, so
it cannot drift from the real part. It carries the top wall with the SMA
bulkhead bore at its derived height, the corner boss at (36.5, 70), and
slivers of the GPS and PN532 pockets where they cross the cut. Print it
second, to check that the module's edge-mounted SMA actually lines up with
the bulkhead hole -- which is the half of "does the NRF24 fit" that the
ladder cannot answer.

Neither coupon changes the enclosure. Whatever the ladder says still has
to be applied to `pockets()` by hand.

## Before laying anything out

**Build it by hand first and bring it up.** A PCB freezes the pin map, and
three things could still move it:

1. The **GPIO 1 GPS handover** is reasoned from the core source and the
   datasheet, with nothing measured. If it does not work, the GPS moves to a
   different pin and J1 changes.
2. The **SpiBus prediction** — that touch loses the bus to the radios — is
   likewise untested. If the fix is wrong, the arrangement changes.
3. `Spotter`'s OUI signatures come from public research, not captured
   packets.

None of those are reasons not to design the board. They are reasons to order
it after bring-up rather than before.

## Suggested first-spin scope

Two layers, 1.6 mm, 1 oz. JLCPCB or PCBWay will do five for the price of
lunch. Fit:

- J1 and the CYD pigtail
- Power tree with the separate RF rail
- Module headers at the enclosure-fixed positions
- R1, the series-resistor footprints, the decoupling
- Test points on +5V_SW, +3V3_RF, SCK, MISO and GPIO 1

Leave off anything not needed to prove the above. The second spin is for
what bring-up teaches you.

### Deliberately not fitted: a sub-GHz PA

The stock CC1101 transmits at +12 dBm. An Ebyte E07-433M20S reaches roughly
+20 dBm on the same 433 MHz work, and it drops in beside a CC1101 on a board
this size. Not taken here, for three reasons:

- It needs two control lines, TX_EN and RX_EN, and there is no spare pair on
  this board. The obvious candidates are gone: GPIO 4 is the RGB LED's red
  channel, and GPIO 0 is a strapping pin that
  decides boot mode. The RGB LED gave up its three pins and UART0 gave up a
  fourth, so fitting a PA means J1 grows and something else moves.
- It moves the power budget. +20 dBm on the RF rail is a different peak draw
  from the one the buck and the 0.5 mm +3V3_RF pour were sized for, and that
  sizing is still marked [verify].
- Transmit testing happens in a shielded enclosure, where 8 dB buys nothing.

Worth revisiting only if the sub-GHz work ever moves outside a cage, and
then as a second-spin change with the pin budget reopened.
