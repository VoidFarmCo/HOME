#!/usr/bin/env python3
"""Every label in a drawing fits its box, and no two labels sit on top of
each other.

`docs/pueo/wiring.svg` is hand-written SVG, and the failure it invites is
silent. A connector gets renamed, its label gets one character longer, and the
text now runs past the box it belongs to or straight through its neighbour.
Nothing errors. Nothing fails to build. The diagram just becomes unreadable,
and only at the one place a reader most needs it to be exact, which for a
wiring diagram is every place.

The three faults that prompted this were on the website's drawings on
2026-09-27: two connector sublabels printed on top of each other, a third ran
into a box, and a fourth was 70px of text in a 66px box. All had been live for
some time and all were found by eye. This diagram was clean when the check was
first pointed at it and the point is to keep it that way.

    python tools/check_svg_labels.py

Reads local files only; connects to nothing.

`check_text_fits.py` is the same question asked of the firmware's own screens,
where TFT_eSPI draws a too-wide string off both edges rather than clipping it.
Same class of fault, different surface: nothing complains, and the result is
only wrong to look at.

THIS FILE HAS A TWIN. The website repo carries the same logic as
`tools/check-svg-labels.py`, because it has six more drawings and the two
repos share nothing. Fix a bug in one and fix it in the other; the two differ
only in this docstring, the default path and the exemption list.

How the widths are known without a browser
------------------------------------------
Every one of these drawings sets `JetBrains Mono, Consolas, monospace`, and
in a monospaced face the advance is the same for every glyph, so a label's
width is just its character count. The ratio was calibrated by measuring
nine real labels with getBBox() in a browser against the served pages:

    buck-boost  9.5px  0.601      J1       9.5px  0.605
    to P3       7.5px  0.597      S7V8F3   8.0px  0.600
    9-way       7.5px  0.611      +3V3_RF  7.5px  0.608
    R1 1k       7.5px  0.611      to P1    7.5px  0.611

so ADVANCE is set to the top of that range and a tolerance absorbs the rest.
Predictions therefore sit at or a hair above reality, which is the direction
a checker should err in.

Which box a label belongs to is decided by its anchor point rather than by
the middle of the text, because those two agree right up until the moment a
label outgrows its box, which is the moment worth catching.

Vertical extents use cap height rather than the full em box on purpose. Two
stacked lines of a side note are 13px apart at 10px type, and their em boxes
touch by a fifth of a pixel: a real overlap arithmetically and no overlap at
all to a reader. The same tolerance applies to both axes for the same reason,
a heading's descender grazing the cap of a label below it by a quarter of a
pixel being the case that proved it.

Takes an optional directory or single file to check instead of docs/pueo. A
directory is read for both .svg, where the drawings stand alone as they do
here, and .html, where they are inline as they are on the website.
"""
import glob
import html
import os
import re
import sys

DOCS = os.path.join(os.path.dirname(os.path.dirname(os.path.abspath(__file__))),
                    "docs", "pueo")

ADVANCE = 0.61     # em per character, monospace, calibrated above
ASCENT = 0.72      # cap height above the baseline, in em
DESCENT = 0.22     # below the baseline, in em
TOL = 1.0          # px of slack before anything is called a failure

# (file, label prefix) pairs that are deliberately wider than the box they sit
# in, for labels that annotate an opening rather than name a part. Nothing in
# this repo's drawing needs one; the website's twin has two.
ALLOW_WIDE = set()

SVG_RE = re.compile(r"<svg\b.*?</svg>", re.S)
TEXT_RE = re.compile(r"<text\b([^>]*)>(.*?)</text>", re.S)
RECT_RE = re.compile(r"<rect\b([^>]*?)/?>", re.S)
ATTR_RE = re.compile(r"([\w:-]+)\s*=\s*\"([^\"]*)\"")
TAG_RE = re.compile(r"<[^>]*>")


def attrs(blob):
    return dict(ATTR_RE.findall(blob))


def num(d, key, default=None):
    try:
        return float(d[key])
    except (KeyError, ValueError):
        return default


def label_box(a, body):
    """Predicted (x1, x2, y1, y2) for one <text>, or None to skip it."""
    if "rotate" in a.get("transform", ""):
        return None                      # rotated rail labels, different axis
    x, y = num(a, "x"), num(a, "y")
    if x is None or y is None:
        return None
    fs = num(a, "font-size", 10.0)
    text = html.unescape(TAG_RE.sub("", body)).strip()
    if not text:
        return None
    w = len(text) * fs * ADVANCE
    anchor = a.get("text-anchor", "start")
    if anchor == "middle":
        x1 = x - w / 2
    elif anchor == "end":
        x1 = x - w
    else:
        x1 = x
    return text, x1, x1 + w, y - fs * ASCENT, y + fs * DESCENT, x, y


def check_svg(svg, page):
    """Return a list of complaint strings for one drawing."""
    rects = []
    for blob in RECT_RE.findall(svg):
        a = attrs(blob)
        x, y = num(a, "x"), num(a, "y")
        w, h = num(a, "width"), num(a, "height")
        if None not in (x, y, w, h):
            rects.append((x, x + w, y, y + h, w * h))

    labels = []
    for blob, body in TEXT_RE.findall(svg):
        got = label_box(attrs(blob), body)
        if got:
            labels.append(got)

    out = []

    # 1. a label placed inside a box has to fit inside it.
    #
    # The box is found from the label's ANCHOR, not from the middle of the
    # text it currently holds. Those agree until a label grows, and then the
    # centre walks out of the box while the anchor stays where the author put
    # it -- so keying on the centre stops checking a label at precisely the
    # size where it started overflowing. A mutation that narrowed a zone box
    # went unreported until this used the anchor instead.
    for text, x1, x2, y1, y2, ax, ay in labels:
        if any(text.startswith(p) for f, p in ALLOW_WIDE if f == page):
            continue
        hosts = [r for r in rects if r[0] <= ax <= r[1] and r[2] <= ay <= r[3]]
        if not hosts:
            continue
        hx1, hx2 = min(hosts, key=lambda r: r[4])[:2]   # the SMALLEST one
        if x1 < hx1 - TOL or x2 > hx2 + TOL:
            out.append("%-28s %.1f wide, its box is %.1f"
                       % ('"%s"' % text, x2 - x1, hx2 - hx1))

    # 2. no two labels may share space
    for i in range(len(labels)):
        for j in range(i + 1, len(labels)):
            a, b = labels[i], labels[j]
            dx = min(a[2], b[2]) - max(a[1], b[1])
            dy = min(a[4], b[4]) - max(a[3], b[3])
            if dx > TOL and dy > TOL:
                out.append("%-28s overlaps %s by %.1f px"
                           % ('"%s"' % a[0], '"%s"' % b[0], dx))
    return out


def main():
    # A checker that dies printing its own finding is worse than no checker,
    # and these labels carry middle dots and arrows.
    try:
        sys.stdout.reconfigure(errors="replace")
    except (AttributeError, ValueError):
        pass
    root = sys.argv[1] if len(sys.argv) > 1 else DOCS
    if os.path.isdir(root):
        pages = sorted(glob.glob(os.path.join(root, "*.html"))
                       + glob.glob(os.path.join(root, "*.svg")))
    else:
        pages = [root]                   # a single file, html or svg
    if not pages:
        print("  nothing to check in %s" % root)
        return 0
    drawings = 0
    labels = 0
    problems = []

    for path in pages:
        page = os.path.basename(path)
        with open(path, encoding="utf-8") as f:
            source = f.read()
        for svg in SVG_RE.findall(source):
            drawings += 1
            labels += len(TEXT_RE.findall(svg))
            for msg in check_svg(svg, page):
                problems.append("  FAIL  %-20s %s" % (page, msg))

    if problems:
        print("\n".join(problems))
        print()
        print("FAILED: %d in %d labels across %d drawings"
              % (len(problems), labels, drawings))
        return 1

    print("  ok    %d labels across %d drawings fit their boxes and clear "
          "each other" % (labels, drawings))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
