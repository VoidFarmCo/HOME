#!/usr/bin/env python3
"""Mockups for a menu that is not upstream's, drawn in the panel's own fonts.

Not a web mockup. This imports render_screens.py's Tft, so every glyph is the
panel's 5x7 GLCD font or its 16 px proportional one, every colour is RGB565
converted once, and the canvas is the real 320x480. A design that does not
fit cannot be drawn here, which is the point: the current layout's problem is
how it spends pixels, and that argument cannot be had in a mockup tool that
has more of them than the panel does.

    python tools/mockups.py [--out dir] [--scale 3]

Three directions, each answering something the current menu does badly:

  instrument  The tiles say what hardware is present and what is running.
              Today the only way to learn the CC1101 is not fitted is to open
              SubGHz and be told.

  list        No grid. The panel is 320x480, which is a list shape, and the
              submenus already paginate at six items while leaving 45% of the
              screen black. Everything on one scrolling list, nothing paged.

  field       The home screen is a readout rather than a launcher: what is in
              range, what is logging, what is nearly out of battery. The menu
              is one tap away. For a device whose job is to tell you what is
              around you, a grid of buttons is a screen that says nothing.

Nothing here is wired to the firmware. They are pictures, drawn to argue
about, and the file is named for that.
"""
import argparse
import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import render_screens as R
from render_screens import (W, H, rgb, UI_BG, UI_FG, UI_LINE, UI_TEXT,
                            UI_ICON, UI_LABLE, WHITE, GREEN, status_bar)

DIM = rgb(0x8410)
RED = rgb(0xF800)
AMB = UI_ICON
OFF = rgb(0x4208)        # a tile whose hardware is not there
BAR = UI_LABLE


# ───────────────────────────────────────────────────────── direction one
def mock_instrument(t, brand):
    """Tiles that carry state.

    Same grid, so the change is small enough to argue for, but each tile
    gains one line underneath the label. Absent hardware drops to a dim
    outline rather than looking identical to the rest until you tap it.
    """
    t.fill_screen(UI_BG)
    status_bar(t, 20)

    # label, icon, state line, colour of that line, present
    TILES = [
        ("WiFi",     "bitmap_icon_wifi",      "11 features",      DIM,   True),
        ("Bluetooth", "bitmap_icon_spoofer",  "11 features",      DIM,   True),
        ("NRF24",    "bitmap_icon_jammer",    "no module",        OFF,   False),
        ("SubGHz",   "bitmap_icon_analyzer",  "no CC1101",        OFF,   False),
        ("Detect",   "bitmap_icon_eye",       "4 hits, 1 dwell",  AMB,   True),
        ("RFID/NFC", "bitmap_icon_rfid_chip", "no PN532",         OFF,   False),
        ("GPS",      "bitmap_icon_satellite", "no fix",           OFF,   False),
        ("System",   "bitmap_icon_setting",   "SD 29.4 GB",       DIM,   True),
    ]

    tw, th = 143, 82
    x0, x1, ytop, ystep = 8, 169, 26, 88
    for i, (label, icon, state, scol, present) in enumerate(TILES):
        col, row = i // 4, i % 4
        x = x0 if col == 0 else x1
        y = ytop + row * ystep
        sel = (i == 4)

        if sel:
            t.fill_round_rect(x, y, tw, th, 5, UI_ICON)
            fg, lab = UI_BG, UI_BG
        else:
            t.fill_round_rect(x, y, tw, th, 5, UI_FG if present else UI_BG)
            t.draw_round_rect(x, y, tw, th, 5, UI_LINE if present else OFF)
            fg = UI_ICON if present else OFF
            lab = UI_TEXT if present else OFF

        t.draw_bitmap_scaled(x + (tw - 32) // 2, y + 8, icon, 16, 16, fg, 2)
        t.print_f2(x + (tw - t.text_width(label)) // 2, y + 44, label, lab,
                   UI_ICON if sel else (UI_FG if present else UI_BG))
        sc = UI_BG if sel else scol
        t.print_f1(x + (tw - len(state) * 6) // 2, y + 66, state, sc,
                   UI_ICON if sel else (UI_FG if present else UI_BG))


# ───────────────────────────────────────────────────────── direction two
def mock_list(t, brand):
    """No grid, no pages.

    The Bluetooth menu is eleven entries split across two pages, and page one
    leaves 45% of the panel black. At this pitch nineteen rows fit. Groups are
    headers rather than a second screen, so nothing is two taps deep that was
    one tap deep before.
    """
    t.fill_screen(UI_BG)
    status_bar(t, 20)

    ROWS = [
        ("hdr", "WIFI", "bitmap_icon_wifi", ""),
        ("row", "Packet Monitor", "", "pcap"),
        ("row", "WiFi Scanner", "", ""),
        ("row", "Deauth Detector", "", ""),
        ("row", "AP Tracker", "", "ch 6"),
        ("hdr", "BLUETOOTH", "bitmap_icon_spoofer", ""),
        ("row", "BLE Scanner", "", ""),
        ("row", "AirTag Sniffer", "", ""),
        ("row", "Hunt", "", ""),
        ("row", "Fast Pair", "", ""),
        ("hdr", "DETECT", "bitmap_icon_eye", ""),
        ("sel", "Surveillance", "", "4 hits"),
        ("row", "Drone Detector", "", ""),
        ("hdr", "SUB-GHZ", "bitmap_icon_analyzer", "no CC1101"),
        ("off", "Replay Attack", "", ""),
        ("off", "Jamming Detector", "", ""),
        ("hdr", "SYSTEM", "bitmap_icon_setting", ""),
        ("row", "File Transfer", "", ""),
        ("row", "Settings", "", ""),
    ]

    y = 24
    for kind, label, icon, right in ROWS:
        if kind == "hdr":
            t.fill_rect(0, y, W, 16, UI_FG)
            if icon:
                t.draw_bitmap(6, y + 2, icon, 16, 16, UI_ICON)
            t.print_f1(26, y + 4, label, UI_ICON, UI_FG)
            if right:
                t.print_f1(W - 8 - len(right) * 6, y + 4, right, OFF, UI_FG)
            y += 18
            continue

        bg = UI_BG
        col = UI_TEXT
        if kind == "sel":
            t.fill_rect(0, y, W, 20, UI_ICON)
            bg, col = UI_ICON, UI_BG
        elif kind == "off":
            col = OFF
        t.print_f2(14, y + 1, label, col, bg)
        if right:
            rc = UI_BG if kind == "sel" else AMB
            t.print_f1(W - 8 - len(right) * 6, y + 6, right, rc, bg)
        y += 22


# ─────────────────────────────────────────────────────── direction three
def mock_field(t, brand):
    """A readout, not a launcher.

    What a passive detector knows is the thing worth putting on its home
    screen. The menu is the left button; everything here is live.
    """
    t.fill_screen(UI_BG)
    status_bar(t, 20)

    t.print_f1(8, 26, "LISTENING", GREEN, UI_BG)
    t.print_f1(W - 8 - 11 * 6, 26, "14m 02s", DIM, UI_BG)
    t.draw_fast_hline(0, 40, W, UI_LINE)

    # the headline: what is in range
    t.print_f2(8, 48, "In range", UI_ICON, UI_BG)
    rows = [
        ("ALPR", "Flock Safety", "-58", RED),
        ("BODYCAM", "Axon", "-49", RED),
        ("TRACKER", "Find My", "-71", AMB),
        ("VEHICLE", "KARR", "-63", AMB),
    ]
    y = 72
    for kind, who, rssi, c in rows:
        t.print_f1(10, y, kind, c, UI_BG)
        t.print_f1(84, y, who, UI_TEXT, UI_BG)
        t.print_f1(W - 8 - 3 * 6, y, rssi, DIM, UI_BG)
        y += 16

    y += 6
    t.draw_fast_hline(0, y, W, UI_LINE)
    y += 8

    # the dwell alarm is the thing that matters most, so it gets a block
    t.fill_round_rect(8, y, W - 16, 42, 4, UI_FG)
    t.draw_round_rect(8, y, W - 16, 42, 4, AMB)
    t.print_f2(16, y + 4, "1 dwelling 11 min", AMB, UI_FG)
    t.print_f1(16, y + 26, "Find My  C2:4A:19:E2  walk to check", DIM, UI_FG)
    y += 52

    # what is being written
    t.print_f2(8, y, "Logging", UI_ICON, UI_BG)
    y += 24
    for label, val in (("/pueo/logs", "spotter_4412.jsonl"),
                       ("card", "29.4 GB free"),
                       ("wardrive", "not running")):
        t.print_f1(10, y, label, DIM, UI_BG)
        t.print_f1(110, y, val, UI_TEXT, UI_BG)
        y += 16

    # the button bar the rest of the firmware already draws
    t.fill_rect(0, H - 26, W, 26, UI_FG)
    t.draw_fast_hline(0, H - 26, W, UI_LINE)
    t.print_f1(10, H - 17, "MENU", UI_ICON, UI_FG)
    t.centre_f1("HUNT", W // 2, H - 17, UI_TEXT, UI_FG)
    t.print_f1(W - 10 - 6 * 6, H - 17, "FILTER", UI_TEXT, UI_FG)


# ───────────────────────────────────────────────── the one being replaced
def mock_today(t, brand):
    """The Bluetooth menu as it is, for the comparison.

    Six of eleven entries, 45% of the panel black, and a Next Page button to
    reach the other five.
    """
    t.fill_screen(UI_BG)
    status_bar(t, 20)
    items = [("bitmap_icon_jammer", "BLE Jammer"),
             ("bitmap_icon_spoofer", "BLE Spoofer"),
             ("bitmap_icon_apple", "Sour Apple"),
             ("bitmap_icon_tag", "AirTag Spoofer"),
             ("bitmap_icon_search", "AirTag Sniffer"),
             ("bitmap_icon_analyzer", "Sniffer")]
    y = 30
    for i, (icon, label) in enumerate(items):
        c = UI_ICON if i == 3 else UI_TEXT
        try:
            t.draw_bitmap(10, y + 2, icon, 16, 16, c)
        except Exception:
            pass
        t.print_f2(30, y, "| " + label, c, UI_BG)
        y += 30
    t.print_f2(10, H - 30, "Main Menu", UI_TEXT, UI_BG)
    t.print_f2(W - 10 - t.text_width("Next Page"), H - 30, "Next Page",
               UI_TEXT, UI_BG)


def mock_grid3(t, brand):
    """B again, finger first.

    The list was 22 px a row, which is 3.4 mm on a 165 ppi panel against a
    7 mm minimum, and the rows are tap targets rather than something a cursor
    steps through. It was unusable and the objection was right.

    A tile is the only shape on this panel that is already finger sized, so
    the fix is more tiles rather than more rows. Three columns by five is 96
    by 78 px, which is 14.8 by 12.0 mm: comfortably past the minimum, and
    fifteen of them, so the twelve-entry WiFi menu lands on one screen with
    no Next Page.

    The Detect submenu is already a grid, so this is the pattern the codebase
    has rather than a new one, applied everywhere instead of once.
    """
    t.fill_screen(UI_BG)
    status_bar(t, 20)

    t.print_f1(8, 26, "WIFI", UI_ICON, UI_BG)
    t.print_f1(W - 8 - 11 * 6, 26, "12 features", DIM, UI_BG)
    t.draw_fast_hline(0, 38, W, UI_LINE)

    ITEMS = [
        ("Packet Mon", "bitmap_icon_wifi", True), ("Beacon Spam", "bitmap_icon_spoofer", True),
        ("Deauther", "bitmap_icon_jammer", True), ("Probe Flood", "bitmap_icon_wifi", True),
        ("Deauth Det", "bitmap_icon_eye", True), ("Scanner", "bitmap_icon_search", True),
        ("Captive", "bitmap_icon_wifi", True), ("Hidden SSID", "bitmap_icon_search", True),
        ("WPS Scan", "bitmap_icon_wifi", True), ("ARP Scan", "bitmap_icon_wifi", True),
        ("Karma", "bitmap_icon_spoofer", True), ("AP Tracker", "bitmap_icon_satellite", True),
    ]

    cols, gap = 3, 8
    tw = (W - gap * (cols + 1)) // cols
    th = 78
    y0 = 44
    for i, (label, icon, present) in enumerate(ITEMS):
        c, r = i % cols, i // cols
        x = gap + c * (tw + gap)
        y = y0 + r * (th + 6)
        sel = (i == 11)
        if sel:
            t.fill_round_rect(x, y, tw, th, 5, UI_ICON)
            ic, lc, bg = UI_BG, UI_BG, UI_ICON
        else:
            t.fill_round_rect(x, y, tw, th, 5, UI_FG)
            t.draw_round_rect(x, y, tw, th, 5, UI_LINE)
            ic, lc, bg = UI_ICON, UI_TEXT, UI_FG
        try:
            t.draw_bitmap_scaled(x + (tw - 32) // 2, y + 12, icon, 16, 16, ic, 2)
        except Exception:
            pass
        t.print_f1(x + (tw - len(label) * 6) // 2, y + 54, label, lc, bg)

    # the footer the rest of the firmware already draws, at its real height
    t.fill_rect(0, H - 34, W, 34, UI_FG)
    t.draw_fast_hline(0, H - 34, W, UI_LINE)
    t.print_f2(10, H - 26, "Main Menu", UI_TEXT, UI_FG)
    lab = "no next page"
    t.print_f1(W - 10 - len(lab) * 6, H - 22, lab, DIM, UI_FG)


SCREENS = [
    ("today-bluetooth", mock_today),
    ("b2-grid3", mock_grid3),
    ("a-instrument", mock_instrument),
    ("b-list", mock_list),
    ("c-field", mock_field),
]


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(R.REPO, "render", "mockups"))
    ap.add_argument("--scale", type=int, default=3)
    a = ap.parse_args()
    R.set_panel(35)

    bitmaps = R.load_bitmaps()
    glcd = R.load_glcd()
    fw, fg = R.load_font16()
    brand = R.load_branding()
    R.BRAND = brand

    os.makedirs(a.out, exist_ok=True)
    for name, fn in SCREENS:
        t = R.Tft(glcd, fw, fg, bitmaps)
        fn(t, brand)
        p1 = os.path.join(a.out, "mock-%s.png" % name)
        t.im.save(p1)
        t.im.resize((W * a.scale, H * a.scale),
                     R.Image.NEAREST).save(
            os.path.join(a.out, "mock-%s@%dx.png" % (name, a.scale)))
        print("  %s" % os.path.basename(p1))
    print("\n%d mockups in %s" % (len(SCREENS), a.out))
    return 0


if __name__ == "__main__":
    sys.exit(main())
