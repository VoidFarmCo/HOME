#!/usr/bin/env python3
"""The screenshots on the website are drawn from the firmware's own numbers.

tools/render_screens.py is not a mockup. It parses the firmware's bitmaps and
TFT_eSPI's fonts and replays the drawing calls at the coordinates the source
gives -- which is what makes the pictures evidence rather than illustration.

It cannot include the firmware's headers to get those coordinates. It is
Python and the constants are C, so it carries its own copy, and a copy is a
thing that drifts.

Nothing would tell you when it did. The renderer would keep producing
plausible screenshots, the pictures on the site would keep looking like the
device, and they would be showing a layout the firmware had stopped using.
That is worse than no pictures: a wrong screenshot is believed.

This compares the two sides, constant by constant. Every entry below is a
number that exists twice on purpose.

    python tools/check_render_sync.py

Reads source; needs no board.
"""
import ast
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"
RENDER = REPO / "tools" / "render_screens.py"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def c_consts(src, names, defines=None):
    """{name: value} for top-level C constants and object-like defines.

    A value is not always a literal: one define can name another, and one
    level of that is resolved from `defines`.

    This used to walk `#if PUEO_PANEL_35` / `#else` arms and return a pair per
    name, because every constant here existed twice. There is one panel now,
    so there is one value, and the branch tracking went with the panel.
    """
    defines = defines or {}
    out = {n: None for n in names}
    for line in src.splitlines():
        t = line.strip()
        m = re.match(r"(?:static\s+)?(?:const|constexpr)\s+u?int\w*\s+(\w+)"
                     r"\s*=\s*(-?\d+)\s*;", t)
        if not m:
            # The trailing comment is not optional decoration to skip over:
            # requiring end-of-line here once made PUEO_BODY_LINE, _GAP and
            # _SIZE invisible, because they are the ones carrying the note
            # that explains them.
            m = re.match(r"#define\s+(\w+)\s+(-?\w+)\s*(?://|/\*|$)", t)
        if not m or m.group(1) not in out:
            continue
        v = m.group(2)
        if not v.lstrip("-").isdigit():
            v = defines.get(v)
            if v is None:
                continue
        out[m.group(1)] = int(v)
    return out


def c_plain_defines(src, names):
    """Unguarded `#define NAME <int>`, for resolving the indirection above."""
    out = {}
    for n in names:
        m = re.search(r"^#define\s+%s\s+(-?\d+)\s*$" % n, src, re.M)
        if m:
            out[n] = int(m.group(1))
    return out


def py_consts(path):
    """Every integer set_panel() assigns, read with ast.

    Parsed rather than pattern-matched. set_panel() assigns three ways --
    `A = 1`, `A, B = 1, 2`, and formerly a conditional -- and each way needed
    its own regex. A constant none of them matched came back None, and the
    comparison that followed was None against None, which passes. ast sees
    every form, and a form it cannot fold to an int is simply absent, which
    is a missing key rather than a silent match.
    """
    tree = ast.parse(path.read_text(encoding="utf-8", errors="replace"))
    fn = next((n for n in ast.walk(tree)
               if isinstance(n, ast.FunctionDef) and n.name == "set_panel"),
              None)
    if fn is None:
        return {}
    out = {}

    def store(target, value):
        if isinstance(target, ast.Name) and isinstance(value, ast.Constant) \
           and isinstance(value.value, int) and not isinstance(value.value, bool):
            out[target.id] = value.value

    for node in ast.walk(fn):
        if not isinstance(node, ast.Assign):
            continue
        for tgt in node.targets:
            if isinstance(tgt, ast.Tuple) and isinstance(node.value, ast.Tuple) \
               and len(tgt.elts) == len(node.value.elts):
                for t, v in zip(tgt.elts, node.value.elts):
                    store(t, v)
            else:
                store(tgt, node.value)
    return out


def main():
    ino = read(SKETCH / "ESP32-DIV.ino")
    shared = read(SKETCH / "shared.h")
    utils = read(SKETCH / "utils.cpp")
    spotter = read(SKETCH / "Spotter.cpp")
    fastpair = read(SKETCH / "FastPairScan.cpp")
    hunt = read(SKETCH / "TrackerHunt.cpp")
    drone = read(SKETCH / "DroneScan.cpp")
    rend = read(RENDER)

    py = py_consts(RENDER)

    def compare(label, cvals, pairs):
        for cname, pname in pairs:
            cv, pv = cvals.get(cname), py.get(pname)
            ok("  %-17s %-18s %s" % (label, cname, cv),
               cv is not None and pv is not None and cv == pv,
               "firmware %s, renderer %s (%s)" % (cv, pv, pname))

    print("the menu grid:")
    grid = ["TILE_W", "TILE_H", "COLUMN_WIDTH", "Y_START", "Y_SPACING",
            "TILE_ICON_DY", "TILE_TEXT_DY"]
    compare("ESP32-DIV.ino", c_consts(ino, grid), [(n, n) for n in grid])

    # The submenu grid, written out in both files. The comment in each says
    # the other is held to it by this check, so this is what makes that true
    # rather than a hope.
    print("\nthe submenu grid:")
    sub = ["GRID_COLS", "GRID_ROWS", "GRID_GAP_X", "GRID_GAP_Y",
           "GRID_TILE_H", "GRID_Y0", "GRID_ICON", "GRID_ICON_DY",
           "GRID_TEXT_DY", "GRID_LINE_H", "GRID_FOOT_H"]
    compare("ESP32-DIV.ino", c_consts(ino, sub), [(n, n) for n in sub])

    print("\nthe chrome:")
    chrome = ["PUEO_STATUS_TALL", "PUEO_TILE_ICON"]
    c2 = c_consts(shared, chrome,
                  c_plain_defines(shared, ["PUEO_STATUS_SHORT"]))
    compare("shared.h", c2, [("PUEO_STATUS_TALL", "STATUS_TALL"),
                             ("PUEO_TILE_ICON", "TILE_ICON")])

    print("\nthe body text on the list screens:")
    # Everything under the font -- the line pitch, the band a redraw clears,
    # the size a centred line is scaled to -- has to move with it, on both
    # sides of the fence.
    body = c_consts(shared, ["PUEO_BODY_FONT", "PUEO_BODY_H",
                             "PUEO_BODY_LINE", "PUEO_BODY_GAP",
                             "PUEO_BODY_SIZE"])
    compare("shared.h", body, [("PUEO_BODY_FONT", "BODY_FONT"),
                               ("PUEO_BODY_LINE", "BODY_LINE"),
                               ("PUEO_BODY_SIZE", "BODY_SIZE")])

    # PUEO_BODY_H and PUEO_BODY_GAP have no counterpart -- the renderer does
    # not redraw in place and does not draw the prose screens -- so they are
    # checked against the font instead. A band shorter than the glyph leaves
    # the bottom of the old text on screen, and that is the defect that took
    # four screens.
    fh = {2: 16, 1: 8}
    missing = sorted(n for n, v in body.items() if v is None)
    ok("  every body constant was found", not missing,
       "not resolved: %s" % ", ".join(missing))
    if not missing:
        f, h = body["PUEO_BODY_FONT"], body["PUEO_BODY_H"]
        line, gap = body["PUEO_BODY_LINE"], body["PUEO_BODY_GAP"]
        ok("  PUEO_BODY_H %s covers a font-%s glyph" % (h, f),
           f in fh and h >= fh[f], "font %s is %s px tall" % (f, fh.get(f)))
        ok("  the line pitch %s clears the band %s" % (line, h),
           line >= h, "lines would overlap")
        ok("  the block gap %s is wider than the line pitch" % gap,
           gap > line)

    print("\nthe rows on each list screen:")
    # Surveillance and Fast Pair draw the same three-line row; Hunt's picker
    # draws two. The renderer replays all three.
    rows = [
        ("Spotter.cpp", spotter, [("kRowH", "BODY_ROW"),
                                  ("kLine2", "BODY_LINE"),
                                  ("kLine3", "BODY_LINE3")]),
        ("FastPairScan.cpp", fastpair, [("kRowH", "BODY_ROW"),
                                        ("kLine2", "BODY_LINE"),
                                        ("kLine3", "BODY_LINE3")]),
        ("TrackerHunt.cpp", hunt, [("kRowH", "HUNT_ROW_H"),
                                   ("kRowLine2", "HUNT_ROW_LINE2")]),
    ]
    for fname, src, pairs in rows:
        compare(fname, c_consts(src, [c for c, _ in pairs]), pairs)

    # Surveillance and Fast Pair are the same row by intent -- the comment in
    # FastPairScan.cpp says so. It said so while they disagreed.
    three = ["kRowH", "kLine2", "kLine3"]
    sp = c_consts(spotter, three)
    fp = c_consts(fastpair, three)
    ok("  Surveillance and Fast Pair draw the same row", sp == fp,
       "Spotter %s, Fast Pair %s" % (sp, fp))

    # Three lines have to fit the row they are in, or the last one lands on
    # the next device's first.
    #
    # It is the glyph that has to fit, not the band. PUEO_BODY_H is the glyph
    # plus its leading -- 16 for font 2, which has none -- and the leading is
    # allowed to reach into the top of the next row, because that row's own
    # first line clears it again on the way past.
    if not missing:
        glyph = fh.get(body["PUEO_BODY_FONT"])
        lead = body["PUEO_BODY_H"] - glyph
        for fname, src in (("Spotter.cpp", spotter),
                           ("FastPairScan.cpp", fastpair)):
            c3 = c_consts(src, ["kRowH", "kLine3"])
            rowh = c3["kRowH"]
            ok("  %s: the third line ends at %d in a %d px row"
               % (fname, c3["kLine3"] + glyph, rowh),
               c3["kLine3"] + glyph <= rowh,
               "it runs %d px into the next row's text"
               % (c3["kLine3"] + glyph - rowh))
            ok("  %s: and clears no further than %d px past it" % (fname, lead),
               c3["kLine3"] + body["PUEO_BODY_H"] <= rowh + lead,
               "the band would erase part of the next row")

    print("\nthe panel itself:")
    # Against shared.h rather than a literal, so the canvas cannot be right
    # here while being wrong where the firmware lays out against it.
    dims = c_consts(shared, ["PUEO_SCREEN_W", "PUEO_SCREEN_H"])
    ok("  the canvas is %sx%s, the size shared.h lays out for"
       % (dims["PUEO_SCREEN_W"], dims["PUEO_SCREEN_H"]),
       (py.get("W"), py.get("H"))
       == (dims["PUEO_SCREEN_W"], dims["PUEO_SCREEN_H"]),
       "renderer %sx%s" % (py.get("W"), py.get("H")))

    print("\nshowFeatureMark(), which every feature opens on:")
    body = utils[utils.index("void showFeatureMark("):]
    body = body[:body.index("\n}")]
    cm = re.search(r"PUEO_SCREEN_H - PUEO_MARK_H\) / 2 - (\d+)", body)
    cc = re.search(r"y \+ PUEO_MARK_H \+ (\d+)", body)
    rm = re.search(r"y = \(H - MARK\) // 2 - (\d+)", rend)
    rc = re.search(r"y \+ MARK \+ (\d+)", rend)
    ok("  the mark is lifted by the same amount",
       bool(cm and rm and cm.group(1) == rm.group(1)),
       "firmware -%s, renderer -%s" % (cm.group(1) if cm else "?",
                                       rm.group(1) if rm else "?"))
    ok("  the caption sits the same distance below it",
       bool(cc and rc and cc.group(1) == rc.group(1)),
       "firmware +%s, renderer +%s" % (cc.group(1) if cc else "?",
                                       rc.group(1) if rc else "?"))

    print("\nthe two full-screen alerts:")
    # Both place mark-plus-text as one block. That arithmetic moved once
    # already, and the renderer had to be moved with it by hand.
    for name, src in (("dwellAlert", spotter), ("droneAlert", drone)):
        blk = re.search(r"kBlockH = mh \+ (\d+)", src)
        two = re.search(r"\(PUEO_SCREEN_H - kBlockH\) \* 2 / 3", src)
        ok("  %s: block is mh + %s" % (name, blk.group(1) if blk else "?"),
           blk is not None)
        ok("  %s: two thirds above, one below" % name, two is not None)
    rblk = re.findall(r"y = \(H - \(mh \+ (\d+)\)\) \* 2 // 3", rend)
    ok("  the renderer places both the same way", len(rblk) == 2,
       "found %d of 2" % len(rblk))
    cblk = re.search(r"kBlockH = mh \+ (\d+)", spotter)
    ok("  and by the same number",
       cblk is not None and all(v == cblk.group(1) for v in rblk),
       "firmware +%s, renderer %s" % (cblk.group(1) if cblk else "?", rblk))

    # The four text lines under the mark, which is where the wording lives.
    want = ["6", "26", "46", "64"]
    for name, src in (("dwellAlert", spotter), ("droneAlert", drone)):
        offs = re.findall(r"y \+ mh \+ (\d+)", src)
        ok("  %s: four lines at %s" % (name, offs), offs == want,
           "expected %s" % want)
    roffs = re.findall(r"y \+ mh \+ (\d+)", rend)
    ok("  the renderer draws them at the same offsets",
       roffs == want * 2, str(roffs))

    print("\nthe beacon's auto-stop, in all three places it is said:")
    # Emit.h decides it. The splash prints it, the renderer draws the splash,
    # and added.html describes it. All four said ten minutes for as long as
    # the constant said fifteen -- and the splash is the screen somebody
    # reads in the five seconds before the board starts transmitting, so it
    # was promising to stop a third of the way into the transmit window.
    #
    # Three of the four now derive from the constant, so changing it carries
    # them. The fourth is prose on added.html, in a different repository
    # this script cannot reach: if you change kAutoStopMs, change the page.
    # Said here because that is where somebody changing it will be looking.
    emit = (REPO / "PueoBeacon" / "Emit.h").read_text(encoding="utf-8")
    sketch = (REPO / "PueoBeacon" / "PueoBeacon.ino").read_text(encoding="utf-8")
    rsrc = RENDER.read_text(encoding="utf-8")

    m = re.search(r"kAutoStopMs\s*=\s*(\d+)u?\s*\*\s*(\d+)u?\s*\*\s*(\d+)u?",
                  emit)
    ok("  kAutoStopMs resolves", m is not None, "not found in Emit.h")
    if m:
        mins = (int(m.group(1)) * int(m.group(2)) * int(m.group(3))) // 60000
        print("        Emit.h says %d minutes" % mins)
        # Neither side may carry the number as a literal. A hard-coded
        # "15 min" would pass a value comparison today and drift tomorrow,
        # which is how this got here, so the check is that it is computed.
        for who, src in (("the splash", sketch), ("the renderer", rsrc)):
            lit = re.search(r"lowest power,\s*\d+\s*min", src)
            ok("  %s does not hard-code the minutes" % who, lit is None,
               "found %r" % (lit.group(0) if lit else ""))
        ok("  the splash builds it from kAutoStopMs",
           "kAutoStopMs / 60000" in sketch.replace("u)", ")"))
        ok("  the renderer builds it from kAutoStopMs",
           "beacon_stop_min()" in rsrc and "kAutoStopMs" in rsrc)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("A number moved on one side and not the other. The renderer is")
        print("what the website shows; left alone it will keep drawing the")
        print("layout the firmware used to have.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
