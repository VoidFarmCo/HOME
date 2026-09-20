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

```bash
openscad -D 'PART="lid"' -o lid.stl docs/pueo/pueo-enclosure.scad
```

| | facets | STL |
|---|---|---|
| lid, logo on | 4166 | 2.6 MB |
| lid, `LOGO=false` | 1273 | 670 KB |

Both report `Simple: yes` from CGAL, so both are manifold. The base is
untouched either way. The extra 12 seconds of render time is the outline.

## Not verified

Nothing here has been printed. The stroke widths are measured off the
artwork and the geometry is confirmed manifold, but whether a 0.51 mm groove
at 0.6 mm deep reads well in a given filament, nozzle and layer height is
something only a test print answers.

`docs/pueo/nrf24-fit-test.scad` is the precedent for that: print the small
thing that answers one question before committing to the big one. A 40 × 40
tile carrying just the chin band would settle it for a few grams of
filament.
