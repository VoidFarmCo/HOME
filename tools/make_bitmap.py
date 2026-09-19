#!/usr/bin/env python3
"""Turn image files into the 1-bpp C arrays icon.h uses.

The sketch draws every icon, the boot logo and the loading animation with
TFT_eSPI's drawBitmap(), which wants 1 bit per pixel, MSB first, each row
padded out to a whole byte. That is the same layout image2cpp produces and
the same one Adafruit_GFX documents.

  tools/make_bitmap.py logo.png --name my_logo --size 150x150
  tools/make_bitmap.py frame*.png --name boot_anim --size 100x120

Output goes to stdout; paste it into ESP32-DIV/icon.h, or redirect and
include it. With several inputs the arrays are numbered from 1, matching the
bitmap_icon_skull_loading_1..10 convention.

Sizing matters: displayLogo() and loading() pass fixed width and height to
drawBitmap, so an array that does not match its declared size is drawn as
garbage rather than being rejected. The defaults here are the sizes those two
already use.

  --size WxH     output size (default 150x150)
  --fit          letterbox to preserve aspect (default: stretch)
  --threshold N  0-255 luminance cut, default 128
  --invert       flip black and white
  --alpha N      pixels with alpha below N count as background, default 128
  --crop         trim to the drawn content and pad to square before scaling
  --margin N     percent margin to leave after --crop, default 4
  --preview      also print an ASCII rendering, to check before pasting
"""

import argparse
import re
import sys
from pathlib import Path

try:
    from PIL import Image
except ImportError:
    sys.exit("needs Pillow:  py -3 -m pip install Pillow")


def crop_to_ink(img, threshold, invert, margin_pct):
    """Trim to the drawn content, then pad back out to a square.

    Generated artwork almost always arrives with slack: letterbox bars, an
    uneven margin, or a canvas that is not square. Scaling that straight to
    150x150 wastes pixels on whitespace, which is exactly what this format
    cannot afford.

    The bounding box is taken after thresholding rather than from getbbox(),
    because "white" out of an image generator is usually 253-255 rather than
    255, and getbbox() on a near-white background returns the whole canvas.
    """
    # "Ink" means the pixels that will end up set, which is exactly the test
    # to_mono applies below: on = (v >= threshold) XOR invert. Getting this
    # backwards finds the bounding box of the *background* -- which on a
    # full-bleed canvas is the whole image, so the crop silently does nothing.
    grey = img.convert("L")
    mask = grey.point(lambda v: 255 if ((v < threshold) if invert else (v >= threshold)) else 0)
    bbox = mask.getbbox()
    if not bbox:
        return img  # nothing drawn; leave it alone

    img = img.crop(bbox)
    side = max(img.width, img.height)
    side = int(side * (1.0 + 2.0 * margin_pct / 100.0))
    bg = (0, 0, 0, 255) if not invert else (255, 255, 255, 255)
    canvas = Image.new("RGBA", (side, side), bg)
    canvas.paste(img, ((side - img.width) // 2, (side - img.height) // 2))
    return canvas


def to_mono(path, size, fit, threshold, invert, alpha_cut, crop=False, margin=4):
    img = Image.open(path)
    img = img.convert("RGBA")

    if crop:
        # Flatten first so transparency does not confuse the ink detection.
        flat = Image.new("RGBA", img.size, (255, 255, 255, 255))
        flat.paste(img, (0, 0), img.split()[3])
        img = crop_to_ink(flat, threshold, invert, margin)

    # Flatten transparency onto the background colour, so a transparent PNG
    # does not come out as a solid block.
    bg = Image.new("RGBA", img.size, (0, 0, 0, 255) if not invert else (255, 255, 255, 255))
    mask = img.split()[3].point(lambda a: 255 if a >= alpha_cut else 0)
    bg.paste(img, (0, 0), mask)
    img = bg.convert("L")

    if fit:
        img.thumbnail(size, Image.LANCZOS)
        canvas = Image.new("L", size, 0 if not invert else 255)
        canvas.paste(img, ((size[0] - img.width) // 2, (size[1] - img.height) // 2))
        img = canvas
    else:
        img = img.resize(size, Image.LANCZOS)

    px = img.load()
    bits = []
    for y in range(size[1]):
        row = []
        for x in range(size[0]):
            on = px[x, y] >= threshold
            if invert:
                on = not on
            row.append(on)
        bits.append(row)
    return bits


def pack(bits, width):
    """MSB-first, rows padded to a byte boundary."""
    out = bytearray()
    for row in bits:
        acc = 0
        n = 0
        for on in row:
            acc = (acc << 1) | (1 if on else 0)
            n += 1
            if n == 8:
                out.append(acc)
                acc, n = 0, 0
        if n:
            out.append(acc << (8 - n))   # pad the tail with zeros
    return out


def emit(name, data, w, h, per_line=16):
    lines = ["// '%s', %dx%dpx" % (name, w, h),
             "const unsigned char bitmap_%s [] PROGMEM = {" % name]
    for i in range(0, len(data), per_line):
        chunk = data[i:i + per_line]
        body = ", ".join("0x%02x" % b for b in chunk)
        tail = "," if i + per_line < len(data) else ""
        lines.append("\t" + body + tail + (" " if tail else ""))
    lines.append("};")
    return "\n".join(lines)


def ascii_preview(bits, cols=72):
    step = max(1, len(bits[0]) // cols)
    for y in range(0, len(bits), step * 2):
        print("  " + "".join("#" if bits[y][x] else "."
                             for x in range(0, len(bits[y]), step)))


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("images", nargs="+", type=Path)
    ap.add_argument("--name", required=True)
    ap.add_argument("--size", default="150x150")
    ap.add_argument("--fit", action="store_true")
    ap.add_argument("--threshold", type=int, default=128)
    ap.add_argument("--invert", action="store_true")
    ap.add_argument("--alpha", type=int, default=128)
    ap.add_argument("--preview", action="store_true")
    ap.add_argument("--crop", action="store_true",
                    help="trim to drawn content and pad to square first")
    ap.add_argument("--margin", type=float, default=4,
                    help="percent margin to leave after --crop (default 4)")
    args = ap.parse_args()

    m = re.fullmatch(r"(\d+)x(\d+)", args.size)
    if not m:
        sys.exit("--size wants WxH, e.g. 150x150")
    w, h = int(m.group(1)), int(m.group(2))
    size = (w, h)

    multi = len(args.images) > 1
    total = 0
    for i, path in enumerate(sorted(args.images), 1):
        if not path.exists():
            sys.exit("no such file: %s" % path)
        bits = to_mono(path, size, args.fit, args.threshold, args.invert,
                       args.alpha, args.crop, args.margin)
        data = pack(bits, w)
        name = "%s_%d" % (args.name, i) if multi else args.name
        if args.preview:
            print("// preview of %s (%s)" % (name, path.name))
            ascii_preview(bits)
        print(emit(name, data, w, h))
        total += len(data)

    expect = (w + 7) // 8 * h
    print("", file=sys.stderr)
    print("%d array(s), %d bytes each, %d bytes total"
          % (len(args.images), expect, total), file=sys.stderr)
    if multi:
        print("frames are numbered %s_1..%s_%d"
              % (args.name, args.name, len(args.images)), file=sys.stderr)
    return 0


if __name__ == "__main__":
    sys.exit(main())
