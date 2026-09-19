#!/usr/bin/env python3
"""Draw the placeholder Pueo logo: a front-on owl, built from primitives.

Placeholder artwork, meant to be replaced. It exists so the splash can be
seen working on real hardware before anyone commits to drawing something,
and so the whole path -- art, converter, icon.h, Branding.h, flash -- gets
exercised end to end.

Everything here is chosen for a 1-bit mask at 150x150 on a 143 PPI panel:
solid silhouette, features cut out of it rather than drawn on top, nothing
finer than about three pixels. No greyscale, no anti-aliasing -- the target
has one ink colour and a threshold, so shading would only turn to mud.

  py -3 tools/make_placeholder_logo.py -o logo.png
  py -3 tools/make_bitmap.py logo.png --name pueo_logo --size 150x150
"""

import argparse
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError:
    raise SystemExit("needs Pillow:  py -3 -m pip install Pillow")

W = H = 150
ON, OFF = 255, 0


def draw(size=(W, H)):
    img = Image.new("L", size, OFF)
    d = ImageDraw.Draw(img)
    sx, sy = size[0] / W, size[1] / H

    def box(x0, y0, x1, y1):
        return [x0 * sx, y0 * sy, x1 * sx, y1 * sy]

    def poly(pts):
        return [(x * sx, y * sy) for x, y in pts]

    # Ear tufts. The pueo is a short-eared owl, so these stay stubby --
    # tall tufts would read as a great horned owl.
    d.polygon(poly([(42, 40), (52, 16), (66, 36)]), fill=ON)
    d.polygon(poly([(108, 40), (98, 16), (84, 36)]), fill=ON)

    # Body and wings first, so the head overlaps them cleanly.
    d.ellipse(box(40, 74, 110, 146), fill=ON)
    d.ellipse(box(24, 76, 58, 140), fill=ON)
    d.ellipse(box(92, 76, 126, 140), fill=ON)

    # Head.
    d.ellipse(box(30, 22, 120, 104), fill=ON)

    # Facial disc: a cut ring that separates the face from the head mass.
    d.ellipse(box(38, 30, 112, 100), outline=OFF, width=max(2, int(3 * sx)))

    # Eyes cut out of the silhouette, pupils punched back in. Cutting the
    # features rather than drawing them is what keeps this readable at 1 bit.
    for cx in (60, 90):
        d.ellipse(box(cx - 17, 45, cx + 17, 79), fill=OFF)
        d.ellipse(box(cx - 8, 54, cx + 8, 70), fill=ON)

    # Beak.
    d.polygon(poly([(75, 72), (68, 88), (82, 88)]), fill=OFF)
    d.polygon(poly([(75, 76), (71, 86), (79, 86)]), fill=ON)

    # Breast streaking, the pueo's most obvious field mark. Chunky enough
    # to survive the threshold instead of dithering into speckle.
    for i, x in enumerate((58, 68, 78, 88)):
        top = 104 + (i % 2) * 6
        d.rounded_rectangle(box(x - 3, top, x + 3, top + 22), radius=3 * sx, fill=OFF)

    # Feet.
    d.polygon(poly([(62, 140), (56, 150), (70, 150)]), fill=ON)
    d.polygon(poly([(88, 140), (80, 150), (94, 150)]), fill=ON)

    return img


def ascii_preview(img, cols=74):
    px = img.load()
    w, h = img.size
    step = max(1, w // cols)
    for y in range(0, h, step * 2):
        print("  " + "".join("#" if px[x, y] else "." for x in range(0, w, step)))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("-o", "--out", type=Path, default=Path("pueo_logo.png"))
    ap.add_argument("--size", type=int, default=W)
    ap.add_argument("--preview", action="store_true")
    args = ap.parse_args()

    img = draw((args.size, args.size))
    img.save(args.out)
    print("wrote %s (%dx%d, 1-bit ready)" % (args.out, args.size, args.size))
    if args.preview:
        ascii_preview(img)


if __name__ == "__main__":
    main()
