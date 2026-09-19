# Pueo carrier board — design notes

Input for a KiCad project, not a layout. Everything here is derived from the
firmware pin map (`ESP32-DIV/board_pueo.h`), the enclosure
(`halehound_v3.scad`), and module datasheets. Figures that need checking
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

Of the ten signals the firmware needs, only four reach a CYD header:

| signal | GPIO | where it is |
|---|---|---|
| CC1101 CS | 27 | CN1 header |
| CC1101 GDO0 | 22 | P3 header |
| CC1101 GDO2 | 35 | P3 header |
| GPS RX | 1 | P1 JST |
| VSPI SCK | 18 | **microSD slot pin** |
| VSPI MOSI | 23 | **microSD slot pin** |
| VSPI MISO | 19 | **microSD slot pin** |
| NRF24 CSN | 4 | **RGB LED pad** |
| NRF24 CE | 16 | **RGB LED pad** |
| PN532 SS | 17 | **RGB LED pad** |

The SPI bus itself is not exposed. Six of ten signals require soldering to
pads or module pins on the CYD, so any carrier board needs a short pigtail
from those pads to a connector. The gain is that it becomes **six joints in
one documented place** instead of thirty scattered across four modules — and
an intermittent joint on MISO presents exactly like the bus faults that cost
a week of firmware archaeology, so reducing their count is not cosmetic.

**[verify]** Confirm CN1/P3/P1 pinouts against your actual board before
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

One connector, J1. Suggested: 14-way 1.25 mm JST-GH or a 0.5 mm FFC. GH is
easier to hand-assemble and tolerates the lid opening and closing; FFC is
thinner. Lid hinge/flex cycles argue for GH.

### J1 pinout

| pin | net | to CYD | note |
|---|---|---|---|
| 1 | GND | GND | |
| 2 | GND | GND | two grounds, one at each end of the connector |
| 3 | +5V_SW | 5V in | board feeds the CYD, see power tree |
| 4 | VSPI_SCK | GPIO 18 | SD slot pad |
| 5 | VSPI_MOSI | GPIO 23 | SD slot pad |
| 6 | VSPI_MISO | GPIO 19 | SD slot pad |
| 7 | CC1101_CS | GPIO 27 | CN1 |
| 8 | CC1101_GDO0 | GPIO 22 | P3 |
| 9 | CC1101_GDO2 | GPIO 35 | P3, input-only on the ESP32 side |
| 10 | NRF_CSN | GPIO 4 | LED pad |
| 11 | NRF_CE | GPIO 16 | LED pad |
| 12 | PN532_SS | GPIO 17 | LED pad |
| 13 | GPS_TX | GPIO 1 | **through R1, see below** |
| 14 | GND | GND | |

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

## Power tree

```
  USB-C ──► TP4056 ──► 1S LiPo ──┬──► MT3608 boost ──► +5V_SW ──► CYD (J1.3)
           (1 A chg)             │                          └──► PN532 VCC
                                 └──► 3.3V buck ──► +3V3_RF ──┬─► NRF24
                                      (separate rail)          └─► CC1101
                                                               └─► GT-U7
```

The separate 3.3 V rail is not optional. The PA/LNA modules brown out if
they share the CYD's regulator, which is why the enclosure already allocates
a third power module at (-29, -48).

### Budget

| load | typical | peak | source |
|---|---|---|---|
| CYD (ESP32 + ILI9341 + backlight) | ~200 mA | ~500 mA on WiFi TX | **[verify]** |
| NRF24L01+PA+LNA | 45 mA RX | ~115 mA TX @ +20 dBm | **[verify]** |
| CC1101 | 16 mA RX | ~34 mA TX @ +10 dBm | **[verify]** |
| PN532 | ~10 mA idle | ~100 mA field on | **[verify]** |
| GT-U7 | 30 mA tracking | ~45 mA acquiring | **[verify]** |

Worst case if everything transmits at once is roughly 800 mA, but the SPI
bus is shared and `SpiBus` enforces one owner at a time, so the radios cannot
all be mid-transaction together. A realistic simultaneous peak is **CYD on
WiFi + one radio + GPS ≈ 660 mA**.

Size the boost for 1 A continuous at 5 V. From a 3.7 V cell at ~85%
efficiency that is about 1.6 A drawn from the battery at peak, so the cell
and its protection circuit must tolerate that — many small protection boards
cut out around 2 A, which is closer than it sounds.

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

Four devices on one bus, reached through a cable, is the part of this design
most likely to disappoint.

- Keep module stubs off the SCK/MOSI spine as short as the placement allows.
  Daisy-chain along the spine rather than star-wiring from J1.
- Footprints for **22 Ω series resistors on SCK and MOSI** at the J1 end.
  Fit 0 Ω initially; they are there so ringing is a part swap, not a respin.
- The CC1101 runs at 4 MHz and the NRF24 at up to 10 MHz (`CC1101_SPI_HZ`,
  and the `RF24` constructor). At 10 MHz through a cable, edge rates matter.
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
| 3.3 V buck | -29, -48 | 20 × 12 |
| LiPo pack | -16, -23 | 45 × 34 |
| GT-U7 GPS | 24, -21 | 28 × 27 |
| PN532 V3 | 0, 18 | 43 × 41 |
| CC1101 HW-863 | -23, 61.5 | 15 × 40 |
| NRF24 PA+LNA | 22, 61 | 16 × 41 |

Constraints that follow:

- **SMA bulkheads at x = -23 and x = +22**, 45 mm apart, axis 5 mm above the
  pocket floor, through the top wall. The radio footprints must put their
  board SMAs exactly there.
- **PN532 needs a 1.4 mm floor window** under its coil at (0, 18). Keep
  copper — especially ground pour — out of that footprint's area on both
  layers, or the field is attenuated by your own board.
- **USB-C on the right wall** at y = −62, 20 × 7 cutout.
- **GPS antenna slot** top centre, 21 mm wide, 1.6 mm floor.
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

### Enclosure changes this implies

Not yet applied to `halehound_v3.scad`:

```
MODULES[1]   26 x 19  ->  27 x 17        TP4056 pocket
USBC_W 20, USBC_HT 7  ->  micro-USB      ~8 x 3 mm jack, so the cutout is
                                         oversized and mis-shaped
```

## Before laying anything out

**Build it by hand first and bring it up.** A PCB freezes the pin map, and
nothing in this firmware has run on hardware. Three things could still move
it:

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
