# The owl on the lid

Yes, and it is in `pueo-enclosure.scad` now — debossed into the chin of the
lid, below the screen window. `LOGO = false` turns it off.

The interesting part is not getting a logo onto a box. It is that this
particular logo is **line art rather than a silhouette**, and line art has a
size below which it stops being printable at all.

## What the artwork is

Measured across `img/pueo-logo.png` (462 × 474 px of ink, 49.7% coverage),
run lengths along rows and columns give:

| percentile | stroke |
|---|---|
| 1st | 4 px |
| 5th | 8 px |
| 25th | 14 px |
| median | 24 px |

A run measured horizontally across a diagonal stroke is longer than the
stroke's true perpendicular width, so these are **optimistic**: the real
thin strokes are at or below this.

Scaling the 5th percentile to a printed height, against a 0.4 mm nozzle:

| logo height | 5th pct stroke | |
|---|---|---|
| 20 mm | 0.34 mm | below the nozzle — the wifi arcs and eye rings stop existing |
| 25 mm | 0.42 mm | single trace, fragile |
| **30 mm** | **0.51 mm** | single trace — the shipped size |
| 40 mm | 0.68 mm | single trace |
| 60 mm | 1.01 mm | two perimeters, comfortable |

So 30 mm is a compromise forced by the lid, not a free choice. Below roughly
25 mm the answer is different artwork — a simplified mark — rather than a
smaller copy of this one.

## Why debossed

Because of those numbers, and it is the whole reason the direction was
chosen rather than defaulted to.

A **raised** 0.5 mm rib standing 0.6 mm off a face is one extrusion wide with
nothing bracing it. It prints, and then it shears off in a pocket.

The same feature **cut in** is a narrow channel in otherwise solid top
layers. It may close up slightly where the channel is under one extrusion
width, but it cannot break off. That is the forgiving direction to fail in,
which is the rule the rest of this project uses for anything unverified.

Depth is 0.6 mm into a 2.5 mm lid skin, leaving 1.9 mm under it.

## Where it sits

The lid face is 85 × 170 with a 56 × 92.5 window centred on it, so the free
bands are 36.25 mm at each end. The logo goes in the lower one, centred at
`LOGO_Y = -64`.

At 30 mm tall it is about 29 mm wide, spanning x ±14.6. The screw pillars
are at x ±36.5, so they are never in the way — which is worth stating
because it is the constraint that would bite first if `LOGO_H` were raised.
Raising it past about 34 mm runs into the window edge at y = −46.25.

It is **not** on the base underside, which has more room. The floor there is
4 mm generally but thinned to `NFC_FLOOR = 1.4 mm` under the PN532 coil, and
cutting decoration into a deliberately thin RF window is a bad trade for a
nicer-looking bottom.

## Getting the artwork into OpenSCAD

OpenSCAD cannot usefully extrude a bitmap — `surface()` would give a 512×512
stepped mesh — so the PNG has to become outlines. There is no potrace or
Inkscape on this machine, and taking on a toolchain dependency for one file
is worse than a short tracer.

`tools/trace_logo.py` binarises the PNG, walks marching squares over the 0.5
isocontour, simplifies each loop with Douglas-Peucker, and writes
`docs/pueo/pueo-owl.svg` as one path with `fill-rule="evenodd"` — which is
what keeps the pupils, the eye rings and the counters in **PUEO** as holes
instead of filling them.

**It checks itself.** It rasterises what it wrote and compares that to the
input bitmap, and refuses to write the file if more than 3% of the artwork
disagrees. At the shipped tolerance of 0.4 px the figure is **0.02%**, from
2893 points across 32 loops. A tracer that quietly drops a contour or fills a
hole produces an SVG that looks fine in a viewer and prints wrong, and
nobody would find out until a part came off the bed.

That check earned its place immediately: the first run reported 4.15% and
refused. The fault was in the checking rasteriser, not the trace — the
contour lives in pixel-**centre** coordinates and the rasteriser sampled on
pixel corners, putting a half-pixel error band along every edge. On line art
with this much perimeter that is several percent, and it looks exactly like
a real fault. With the rasteriser fixed the trace matches the source
**bit for bit** at zero tolerance.

The SVG is generated. Re-run the tracer rather than editing it.

## Rendering

The OpenSCAD is not published and is not in the source archive — the STLs at
[pueo.magikh0e.pl/enclosure.html](https://pueo.magikh0e.pl/enclosure.html)
are what is offered. Inside the repository, where the file lives:

```bash
openscad -D 'PART="lid"' -o lid.stl docs/pueo/pueo-enclosure.scad
```

| | facets | STL |
|---|---|---|
| lid, logo on | 4166 | 2.6 MB |
| lid, `LOGO=false` | 1273 | 670 KB |

Both report `Simple: yes` from CGAL, so both are manifold. The base is
untouched either way. The extra 12 seconds of render time is the outline.

## The scale bug, because a render did not catch it

The first version of `owl2d()` scaled the import by `height / 474`, which
would be right if OpenSCAD imported one SVG pixel as one unit. It does not.
It converts px to mm at a fixed 96 dpi, so a 474 px tall file arrives
125.4 mm tall, and scaling *that* by 30/474 gives **7.9 mm**. The centring
translate, written in pixels, landed in millimetres too, so the owl also sat
11 mm off centre.

Rendered at lid scale it looked completely plausible. A small owl in the
chin of a 170 mm lid is just a small owl — nothing in the picture says it is
a quarter of its intended size. Two renders were inspected and both passed.

What caught it was arithmetic: 108,773 ink px at 30 mm tall and 0.6 mm deep
has to remove about **261 mm³**, and measuring the lid's volume with and
without the logo showed **18**.

The fix is `resize([0, height], auto=true)`, which measures the bounding box
that actually turned up rather than assuming one. Passing `dpi=25.4` to
`import()` does *not* work — the SVG's explicit `width="462px"` wins.

`tools/check_logo_scale.py` now renders the artwork at three sizes and
asserts height, width, centring and area against figures derived from the
source bitmap, plus the lid's removed volume. 14 checks. It skips cleanly
when OpenSCAD is absent, since the firmware build does not need it.

## Which way up

**Orientation decides whether the logo reads.** The lid is a tray, so printed
outer-face-up its ceiling is an 80 × 165 bridge. It has to print
outer-face-**down**, which puts the logo against the bed. Bed-side grooves
behave differently from top-side ones: glass-smooth floor, crisp walls, and
the material above bridges a 0.5 mm gap without noticing.

So the tile carries the same owl on **both faces**, back to back in the same
place:

| face | what it tells you |
|---|---|
| underside | the real case — what the lid will get |
| top | the comparison, if top-side detail turns out better |

Print it flat and as-is. Everything on the underside is already mirrored in
the model, so it reads correctly when the tile is turned over.

Alongside the underside owl is a **groove ladder** — eight slots at the
shipped 0.6 mm depth and widths 0.3 to 1.6 mm, labelled. That is the part
that generalises past this logo: the narrowest groove that still has a
visible floor is this printer's limit for a debossed line, and anything
thinner in the artwork is decoration that will not survive. 0.3 is below a
0.4 mm nozzle and is meant to fail.

## Not verified

Nothing here has been printed. Stroke widths are measured off the artwork,
the geometry is confirmed manifold, and the scale is now asserted rather
than eyeballed — but whether a 0.51 mm groove at 0.6 mm deep reads well in a
given filament and layer height is what the tile is for.

`docs/pueo/nrf24-fit-test.scad` is the precedent: print the small thing that
answers one question before committing to the big one.
