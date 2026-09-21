"""Render Pueo's screens as the panel would draw them, to PNG.

Not a mockup. This parses the firmware's own bitmaps out of icon.h and
TFT_eSPI's own font data out of glcdfont.c and Font16.c, implements the
handful of TFT primitives the menus use, and replays the drawing calls at
the coordinates the source gives. The text is the panel's actual 5x7 GLCD
font and its 16 px proportional font, not a web substitute.

    python tools/render_screens.py [--out dir] [--scale 3]

What is exact: every bitmap, every glyph, every colour (RGB565 converted
once), and every coordinate, because they are read from the source rather
than retyped.

What is not: the rounded corners. TFT_eSPI draws them with its own circle
helper and this uses PIL's, which can differ by a pixel at r=5. Nothing else
in these screens has curves.

Colours assume the dark theme and accent preset 0 (Orange), which are the
defaults. Battery is drawn at 85%, and the status bar's live counts are
shown in their "something was heard" state, because a screenshot of an idle
device shows less than one of a working one -- stated here rather than
implied.
"""
import argparse
import math
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ICON_H = os.path.join(REPO, "ESP32-DIV", "icon.h")
FONTS = os.path.join(REPO, ".arduino", "user", "libraries", "TFT_eSPI", "Fonts")

# The panel these are drawn for. main() sets it from --panel; the default
# is the 3.5", which is the board this firmware runs on.
#
# Every layout constant below is the firmware's own, read out of
# ESP32-DIV.ino and shared.h rather than chosen to look right. The menu grid
# in particular is a different size per panel, not the same grid scaled:
# 145x92 tiles on a 155 px column pitch against 100x60 on 120.
PANEL = 35
W, H = 320, 480


def set_panel(panel):
    """Resize the canvas and the layout to one of the two panels."""
    global PANEL, W, H
    global TILE_W, TILE_H, COLUMN_WIDTH, X_OFFSET_RIGHT, Y_START, Y_SPACING
    global TILE_ICON_DY, TILE_TEXT_DY, STATUS_ICONS_W, STATUS_TALL, TILE_ICON
    PANEL = panel
    W, H = (320, 480) if panel == 35 else (240, 320)
    if panel == 35:
        TILE_W, TILE_H, COLUMN_WIDTH = 145, 92, 155
        Y_START, Y_SPACING = 44, 106
        # icon(32) + 6 gap + label(16) = 54, centred in a 92 px tile
        TILE_ICON_DY, TILE_TEXT_DY = 19, 57
    else:
        TILE_W, TILE_H, COLUMN_WIDTH = 100, 60, 120
        Y_START, Y_SPACING = 30, 75
        TILE_ICON_DY, TILE_TEXT_DY = 10, 30
    X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH
    # drawStatusBar()'s right-hand cluster: BLE icon, count, wifi bars, temp,
    # SD, plus gaps and a 4 px margin. Anchored to the right edge.
    STATUS_ICONS_W = 110
    # PUEO_STATUS_TALL in shared.h: the menu grids get a taller bar on the
    # 3.5", whose Y_START is 44. The 2.8"'s is 30 and has no room.
    STATUS_TALL = 34 if panel == 35 else 20
    # PUEO_TILE_ICON in shared.h. The 2.8" tile is 100x60 and has no room.
    TILE_ICON = 32 if panel == 35 else 16


X_OFFSET_LEFT = 10
set_panel(PANEL)


# ── colours, straight from shared.h ────────────────────────────────────────
def rgb(c565):
    r = (c565 >> 11) & 0x1F
    g = (c565 >> 5) & 0x3F
    b = c565 & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


UI_BG = rgb(0x20E4)
UI_FG = rgb(0x3166)
UI_LINE = rgb(0x8410)
UI_TEXT = rgb(0xFFFF)
UI_ICON = rgb(0xFBE4)          # accent preset 0, Orange
UI_LABLE = rgb(0x4208)
GREEN = rgb(0xB721)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
CYAN = rgb(0x07FF)
RED = rgb(0xF800)              # TFT_RED, Conf::Strong
LIGHTGREY = rgb(0xD69A)        # TFT_LIGHTGREY, the address line
DARKGREY = rgb(0x7BEF)         # TFT_DARKGREY, Conf::Weak and the third line
BLUE = rgb(0x001F)             # TFT_BLUE, Hunt's COLDER


def _strip_comments(src):
    """A // comment sits between `=` and `{` in these files, so a regex that
    expects only whitespace there matches nothing. Worse, the width table's
    comments are full of digits ("char 32 - 39") which would be read as
    widths. Take them out before parsing anything."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def _preprocess(src):
    """Resolve the #ifdef/#else pairs in Font16.c.

    Not optional. The width table carries two versions of chars 96-103, one
    behind TFT_ESPI_GRAVE_IS_DEGREE and one behind #else, and reading both
    gives a 104-entry table in which everything past index 64 is shifted by
    eight. The visible result is that narrow letters advance too far --
    "WiFi" comes out as "Wi Fi" -- which looks like a rendering bug rather
    than a parsing one, so it is worth resolving properly instead of special
    casing the one table.
    """
    defined = set(re.findall(r"^\s*#define\s+(\w+)\s*$", src, re.M))
    out, stack = [], []
    for line in src.split("\n"):
        m = re.match(r"\s*#(ifdef|ifndef|else|endif)\s*(\w*)", line)
        if m:
            kind, name = m.group(1), m.group(2)
            if kind == "ifdef":
                stack.append(name in defined)
            elif kind == "ifndef":
                stack.append(name not in defined)
            elif kind == "else":
                stack[-1] = not stack[-1]
            else:
                stack.pop()
            continue
        if all(stack):
            out.append(line)
    return "\n".join(out)


def load_branding():
    """The boot strings, from Branding.h rather than retyped here.

    The version is on the boot screen, so a hardcoded copy means the
    screenshots quietly disagree with the firmware one release after anyone
    stops checking."""
    src = _strip_comments(
        open(os.path.join(REPO, "ESP32-DIV", "Branding.h"),
             encoding="utf-8", newline="").read())
    out = {}
    for name in ("PUEO_VERSION", "PUEO_AUTHOR", "PUEO_TAGLINE",
                 "PUEO_UPSTREAM"):
        m = re.search(r"#define\s+" + name + r'\s+"([^"]*)"', src)
        if not m:
            raise SystemExit("Branding.h has no %s" % name)
        out[name] = m.group(1)
    return out


# ── the firmware's bitmaps ─────────────────────────────────────────────────
def load_bitmaps():
    src = _strip_comments(open(ICON_H, encoding="utf-8", newline="").read())
    out = {}
    for m in re.finditer(
            r"\b(bitmap_\w+)\s*\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S):
        vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
        out[m.group(1)] = vals
    return out


# ── TFT_eSPI font 1: 5x7 GLCD, 5 columns per char, LSB is the top pixel ────
def load_glcd():
    src = _strip_comments(
        open(os.path.join(FONTS, "glcdfont.c"), encoding="utf-8").read())
    body = src[src.index("font[] PROGMEM = {"):]
    vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", body)]
    return [vals[i * 5:(i + 1) * 5] for i in range(256)]


# ── TFT_eSPI font 2: 96 glyphs from ASCII 32, 16 rows, MSB left ────────────
def load_font16():
    src = _preprocess(_strip_comments(
        open(os.path.join(FONTS, "Font16.c"), encoding="utf-8").read()))
    wm = re.search(r"widtbl_f16\[96\]\s*=\s*\{(.*?)\};", src, re.S)
    widths = [int(x) for x in re.findall(r"\b(\d+)\b", wm.group(1))]
    # 96 exactly, or an #ifdef was mishandled and every later glyph shifts
    assert len(widths) == 96, "width table has %d entries, want 96" % len(widths)

    glyphs = {}
    for m in re.finditer(
            r"chr_f16_([0-9a-fA-F]{2})\[\d+\]\s*=\s*\{(.*?)\};", src, re.S):
        code = int(m.group(1), 16)
        glyphs[code] = [int(v, 16)
                        for v in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
    return widths, glyphs


class Tft:
    """Only the primitives the menus actually call."""

    def __init__(self, glcd, f16_widths, f16_glyphs, bitmaps):
        self.im = Image.new("RGB", (W, H), BLACK)
        self.d = ImageDraw.Draw(self.im)
        self.glcd = glcd
        self.fw, self.fg = f16_widths, f16_glyphs
        self.bm = bitmaps

    # -- shapes --
    def fill_screen(self, c):
        self.d.rectangle([0, 0, W - 1, H - 1], fill=c)

    def fill_rect(self, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def fill_round_rect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=c)

    def draw_round_rect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r,
                                 outline=c)

    def draw_fast_hline(self, x, y, w, c):
        self.d.rectangle([x, y, x + w - 1, y], fill=c)

    def draw_fast_vline(self, x, y, h, c):
        self.d.rectangle([x, y, x, y + h - 1], fill=c)

    def draw_rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def draw_pixel(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.im.putpixel((x, y), c)

    def draw_line(self, x0, y0, x1, y1, c):
        self.d.line([x0, y0, x1, y1], fill=c)

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, c):
        self.d.polygon([(x0, y0), (x1, y1), (x2, y2)], fill=c)

    def fill_circle(self, x, y, r, c):
        self.d.ellipse([x - r, y - r, x + r, y + r], fill=c)

    # -- 1bpp bitmap, rows of ceil(w/8) bytes, MSB first --
    def draw_bitmap(self, x, y, name, w, h, c):
        data = self.bm[name]
        stride = (w + 7) // 8
        px = self.im.load()
        for row in range(h):
            for col in range(w):
                i = row * stride + (col >> 3)
                if i < len(data) and data[i] & (0x80 >> (col & 7)):
                    xx, yy = x + col, y + row
                    if 0 <= xx < W and 0 <= yy < H:
                        px[xx, yy] = c

    def draw_bitmap_scaled(self, x, y, name, w, h, c, scale):
        """drawBitmapScaled() in utils.cpp -- the bits doubled, not new art."""
        if scale <= 1:
            self.draw_bitmap(x, y, name, w, h, c)
            return
        data = self.bm[name]
        stride = (w + 7) // 8
        px = self.im.load()
        for row in range(h):
            for col in range(w):
                i = row * stride + (col >> 3)
                if i >= len(data) or not (data[i] & (0x80 >> (col & 7))):
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        xx, yy = x + col * scale + sx, y + row * scale + sy
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = c

    # -- font 1, size 1: 5 columns then a 1px gap --
    def _glcd_char(self, x, y, ch, c, bg):
        cols = self.glcd[ord(ch) & 0xFF]
        px = self.im.load()
        for col in range(6):
            bits = cols[col] if col < 5 else 0
            for row in range(8):
                on = bits & (1 << row)
                xx, yy = x + col, y + row
                if 0 <= xx < W and 0 <= yy < H:
                    if on:
                        px[xx, yy] = c
                    elif bg is not None:
                        px[xx, yy] = bg

    def draw_string_tc(self, s, cx, y, c, bg=None):
        """TC_DATUM: x is the centre of the string, y its top."""
        x = cx - (len(s) * 6) // 2
        for ch in s:
            self._glcd_char(x, y, ch, c, bg)
            x += 6

    def print_f1_size(self, x, y, s, c, bg, size):
        """setTextSize(n) with font 1: the same 5x7 cells, n x n pixels each.

        Written out rather than drawn at 1x and upscaled. TFT_eSPI scales
        the cell, not the string, so the one-pixel gap between characters
        scales with it -- upscaling a finished bitmap puts that gap in the
        wrong place and rounds the glyph edges."""
        cw = 6 * size
        for i, ch in enumerate(s):
            cx = x + i * cw
            for col in range(5):
                bits = self.glcd[ord(ch) & 0xFF][col]
                for row in range(8):
                    if bits & (1 << row):
                        fill = c
                    elif bg is not None:
                        fill = bg
                    else:
                        continue
                    self.d.rectangle(
                        [cx + col * size, y + row * size,
                         cx + col * size + size - 1,
                         y + row * size + size - 1], fill=fill)
            if bg is not None:
                self.d.rectangle([cx + 5 * size, y,
                                  cx + 6 * size - 1,
                                  y + 8 * size - 1], fill=bg)

    def centre_f1(self, s, cx, y, c, bg=None, size=1):
        """drawCentreString(s, x, y, 1): x is the centre, not the left."""
        self.print_f1_size(cx - (6 * size * len(s)) // 2, y, s, c, bg, size)

    def print_f1(self, x, y, s, c, bg=None):
        """Cursor-relative print in font 1, which is what drawStatusBar uses
        for the battery percentage -- it calls setTextFont(1) immediately
        before. Rendering that line in font 2 puts a 16 px glyph cell in a
        20 px bar starting at y=6, which paints two rows of background below
        the bar and looks exactly like a firmware bug. It is not one."""
        for ch in s:
            self._glcd_char(x, y, ch, c, bg)
            x += 6
        return x

    # -- font 2 --
    def text_width(self, s):
        return sum(self.fw[ord(ch) - 32] for ch in s if 32 <= ord(ch) < 128)

    def print_f2(self, x, y, s, c, bg=None):
        px = self.im.load()
        for ch in s:
            code = ord(ch)
            if not (32 <= code < 128):
                continue
            w = self.fw[code - 32]
            glyph = self.fg.get(code)
            stride = (w + 7) // 8
            for row in range(16):
                for col in range(w):
                    i = row * stride + (col >> 3)
                    on = glyph and i < len(glyph) and \
                        glyph[i] & (0x80 >> (col & 7))
                    xx, yy = x + col, y + row
                    if 0 <= xx < W and 0 <= yy < H:
                        if on:
                            px[xx, yy] = c
                        elif bg is not None:
                            px[xx, yy] = bg
            x += w
        return x


# ── drawStatusBar(), 85% battery, something heard on both radios ───────────
def status_bar(t, h=20):
    """drawStatusBar(). h is PUEO_STATUS_SHORT or PUEO_STATUS_TALL.

    Everything is placed off y, exactly as the firmware does it, so the tall
    bar is the one line below and not a second layout."""
    t.fill_rect(0, 0, W, h, UI_LABLE)
    x, y = 7, 4 + (h - 20) // 2
    t.draw_round_rect(x, y, 22, 10, 2, WHITE)
    t.fill_rect(x + 22, y + 3, 2, 4, WHITE)
    t.fill_round_rect(x + 2, y + 2, 85 * 20 // 100, 6, 1, GREEN)
    t.print_f1(x + 30, y + 2, "85%", GREEN, UI_LABLE)

    # Name and version, centred in what is left between the battery block and
    # the icon cluster, and dropped rather than overlapped if it will not fit.
    # Font 2 once the bar is tall enough for 16 px, matching the icons.
    build = "Pueo " + BRAND["PUEO_VERSION"]
    big = h >= 28
    text_w = t.text_width(build) if big else len(build) * 6
    gap_l = x + 30 + 24
    gap_r = W - STATUS_ICONS_W - 4
    if gap_r - gap_l >= text_w:
        bx = gap_l + (gap_r - gap_l - text_w) // 2
        by = (h - (16 if big else 8)) // 2
        (t.print_f2 if big else t.print_f1)(bx, by, build, UI_LINE, UI_LABLE)

    ble_icon_x, gap, icon_w = W - STATUS_ICONS_W, 3, 16
    ble_text_x = ble_icon_x + icon_w + gap
    wifi_bars_x = ble_text_x + 12 + gap
    temp_icon_x = wifi_bars_x + 24 + gap
    sd_icon_x = temp_icon_x + icon_w + gap
    icon_y = y - 2

    wifi_x, wifi_y = wifi_bars_x + 10, y + 11
    for i in range(4):
        bar_h = (i + 1) * 3
        t.draw_round_rect(wifi_x + i * 6, wifi_y - bar_h, 4, bar_h, 1, WHITE)

    t.draw_bitmap(ble_icon_x + 25, icon_y, "bitmap_icon_ble", 16, 16, CYAN)
    t.draw_bitmap(temp_icon_x + 10, y - 2, "bitmap_icon_temp", 16, 16, GREEN)
    t.draw_bitmap(sd_icon_x + 10, y - 2, "bitmap_icon_sdcard", 16, 16, GREEN)


def render_boot(t, brand):
    """displayLogo(TFT_WHITE, 500) in utils.cpp."""
    t.fill_screen(BLACK)
    lw = lh = 200
    lx, ly = (W - lw) // 2, (H - lh) // 2 - 20
    t.draw_bitmap(lx, ly, "bitmap_pueo_logo", lw, lh, WHITE)
    ty = ly + lh + 10
    cx = W // 2
    # PUEO_LOGO_HAS_WORDMARK is 1, so no separate name line
    t.draw_string_tc("by: " + brand["PUEO_AUTHOR"], cx, ty, WHITE)
    ty += 16
    t.draw_string_tc(brand["PUEO_TAGLINE"], cx, ty, WHITE)
    ty += 16
    t.draw_string_tc(brand["PUEO_VERSION"], cx, ty, WHITE)
    ty += 22
    t.draw_string_tc(brand["PUEO_UPSTREAM"], cx, ty, WHITE)


# PUEO_MARK_W / PUEO_MARK_H in Branding.h. Panel-independent: 200 fits both.
MARK = 200


def render_mark(t, bitmap, caption):
    """showFeatureMark() in utils.cpp.

    No status bar -- the mark is drawn over a cleared screen and the feature
    paints its own chrome once the hold is over."""
    t.fill_screen(UI_BG)
    x = (W - MARK) // 2
    y = (H - MARK) // 2 - 14
    t.draw_bitmap(x, y, bitmap, MARK, MARK, UI_ICON)
    tw = t.text_width(caption)
    t.print_f2((W - tw) // 2, y + MARK + 12, caption, UI_TEXT, UI_BG)


MENU = [
    ("WiFi", "bitmap_icon_wifi"), ("2.4GHz", "bitmap_icon_jammer"),
    ("More", "bitmap_icon_dots"), ("Settings", "bitmap_icon_setting"),
    ("Bluetooth", "bitmap_icon_spoofer"), ("SubGHz", "bitmap_icon_analyzer"),
    ("Tools", "bitmap_icon_stat"), ("About", "bitmap_icon_question"),
]


def render_menu(t, selected=0):
    """displayMenu() in ESP32-DIV.ino -- a tile grid, so the tall bar."""
    t.fill_screen(UI_BG)
    for i, (label, icon) in enumerate(MENU):
        col, row = i // 4, i % 4
        x = X_OFFSET_LEFT if col == 0 else X_OFFSET_RIGHT
        y = Y_START + row * Y_SPACING
        sel = (i == selected)
        fill = UI_ICON if sel else UI_FG
        edge = UI_ICON if sel else UI_LINE
        ink = UI_BG if sel else UI_TEXT
        t.fill_round_rect(x, y, TILE_W, TILE_H, 5, fill)
        t.draw_round_rect(x, y, TILE_W, TILE_H, 5, edge)
        t.draw_bitmap_scaled(x + (TILE_W - TILE_ICON) // 2, y + TILE_ICON_DY,
                             icon, 16, 16, ink, TILE_ICON // 16)
        tw = t.text_width(label)
        t.print_f2(x + (TILE_W - tw) // 2, y + TILE_TEXT_DY, label, ink, fill)
    status_bar(t, STATUS_TALL)


BT_PAGE0 = [
    ("BLE Jammer", "bitmap_icon_ble_jammer"),
    ("BLE Spoofer", "bitmap_icon_spoofer"),
    ("Sour Apple", "bitmap_icon_apple"),
    ("AirTag Spoofer", "bitmap_icon_tags"),
    ("AirTag Sniffer", "bitmap_icon_magnifying_glass"),
    ("Sniffer", "bitmap_icon_analyzer"),
    ("BLE Scanner", "bitmap_icon_graph"),
    ("BLE Rubber Ducky", "bitmap_icon_rubber_ducky"),
]


def render_bluetooth(t, selected=3):
    """displayPagedSubmenu(), Bluetooth page 0.

    A list, not a tile grid: rows at 30 + i * 30, so the short bar. Calling
    it a grid in a comment is how it got a tall one that painted over its
    own first row."""
    t.fill_screen(UI_BG)
    for i, (label, icon) in enumerate(BT_PAGE0):
        y = 30 + i * 30
        c = UI_ICON if i == selected else UI_TEXT
        t.draw_bitmap(10, y, icon, 16, 16, c)
        t.print_f2(30, y, "| " + label, c, UI_BG)

    ny = H - 30
    icon_y = ny + (28 - 16) // 2
    text_y = ny + (28 - 16) // 2
    t.draw_bitmap(10, icon_y, "bitmap_icon_go_back", 16, 16, UI_TEXT)
    t.print_f2(30, text_y, "Main Menu", UI_TEXT, UI_BG)
    label = "Next Page"
    icon_x = W - 10 - 16
    t.print_f2(icon_x - 4 - t.text_width(label), text_y, label, UI_TEXT, UI_BG)
    t.draw_bitmap(icon_x, icon_y, "bitmap_icon_navigate_right", 16, 16, UI_TEXT)
    status_bar(t)


# Spotter rows, in the shape drawList() prints them. Each is what the
# detector would hold after hearing the device described in the comment.
#
# conf drives the colour of the first line and nothing else: Strong red,
# Likely orange, Weak dark grey. "**" in the right margin is corroborated --
# two different signatures matched the same address, which is the only way a
# Likely is promoted to Strong.
SPOTTER_HITS = [
    # Flock's own IEEE block, heard on WiFi. Bolted to a pole: 41 minutes in
    # range and 312 probe requests, which is what separates it from a phone.
    dict(kind="ALPR", label="Flock Safety", conf="Strong",
         mac="B4:1E:52:0C:7A:31", via="WiFi", rssi=-58, hits=312,
         fp=0x9E41C7A2, rnd=False, rot=0, age="41m", corrob=False),

    # A Liteon OUI, which alone is worth nothing -- but the same address also
    # beaconed "Flock-2291", and two independent fields agreeing is the whole
    # point of the scoring. Promoted, and marked.
    dict(kind="ALPR", label="Flock SSID", conf="Strong",
         mac="00:F4:8D:11:B2:60", via="WiFi", rssi=-71, hits=96,
         fp=0x2D7F0B54, rnd=False, rot=0, age="39m", corrob=True),

    # 0.3.1: Axon's own block on a public BLE address. No fingerprint,
    # because that is built out of WiFi information elements.
    dict(kind="BODYCAM", label="Axon Enterprise", conf="Strong",
         mac="00:25:DF:4A:19:E2", via="BLE", rssi=-49, hits=18,
         fp=0, rnd=False, rot=0, age="2m", corrob=False),

    # 0.3.3: a KARR module, matched on its advertised name -- "QT " plus
    # exactly eight characters. Likely, not Strong: the name says a module is
    # there, and nothing in the advertisement says whether it still carries
    # the shared key. Also the only orange row here, which is the point of
    # including it -- the grading has three levels and the render showed two.
    dict(kind="VEHICLE", label="KARR BT module", conf="Likely",
         mac="D8:3A:DD:41:0C:96", via="BLE", rssi=-63, hits=27,
         fp=0, rnd=False, rot=0, age="6m", corrob=False),

    # What the same table looks like when it is guessing. A contract
    # manufacturer's block, seen three times in eight seconds, walking past.
    dict(kind="ALPR", label="Liteon (ALPR?)", conf="Weak",
         mac="14:5A:FC:83:D1:07", via="WiFi", rssi=-77, hits=3,
         fp=0x33B1006E, rnd=False, rot=0, age="8s", corrob=False),

    # A randomised address. "rnd" says the OUI on the line above is made up,
    # so the Weak grading is being generous.
    dict(kind="ALPR", label="LAA, not a vendor", conf="Weak",
         mac="82:6B:F2:5E:40:98", via="WiFi", rssi=-69, hits=11,
         fp=0x7C22A1D4, rnd=True, rot=2, age="4m", corrob=False),
]

CONF_COLOUR = {"Strong": RED, "Likely": UI_ICON, "Weak": DARKGREY}


def render_spotter(t):
    """drawHeader() and drawList() in Spotter.cpp.

    Rendered without the touch nav bar, so contentBottom() is the panel
    height and the list gets (H-42)/30 rows -- 14 on the 3.5", 9 on the 2.8".
    With touch buttons enabled the feature reserves the bottom strip for
    Exit/Down/Up/Log and the count drops.
    """
    t.fill_screen(BLACK)
    status_bar(t)

    # drawHeader()
    t.fill_rect(0, 20, W, 18, BLACK)
    t.print_f1(8, 24, "ch  6  frames 18244  hits %d" % len(SPOTTER_HITS),
               WHITE, BLACK)
    t.print_f1(180, 24, "REC 41", RED, BLACK)        # s_logging, rows written

    # drawList()
    top, row_h = 42, 30
    t.fill_rect(0, top, W, H - top, BLACK)
    for i, h in enumerate(SPOTTER_HITS):
        y = top + i * row_h

        t.print_f1(8, y, "%-9s %s" % (h["kind"], h["label"]),
                   CONF_COLOUR[h["conf"]], BLACK)

        t.print_f1(8, y + 11, "%s %s %ddBm x%u"
                   % (h["mac"], h["via"], h["rssi"], h["hits"]),
                   LIGHTGREY, BLACK)

        third = ""
        if h["fp"]:
            third += "fp %08X " % h["fp"]
        if h["rnd"]:
            third += "rnd "
        if h["rot"]:
            third += "+%d " % h["rot"]
        third += h["age"]
        t.print_f1(8, y + 21, third, DARKGREY, BLACK)

        if h["corrob"]:
            t.print_f1(224, y, "**", RED, BLACK)


# ── Hunt ───────────────────────────────────────────────────────────────────
#
# Replays TrackerHunt.cpp's drawPicker() and drawGauge(). Rendered without
# the touch nav bar, so contentBottom() is the panel height -- on the board
# the bar takes the bottom strip and the picker shows two fewer rows.
#
# The addresses are locally-administered (the 0x02 bit set) and invented.
# Real ones would be somebody's tracker, and a Find My address is rotating
# anyway, so a screenshot of one says nothing true for longer than an hour.

HUNT_TARGETS = [
    {"label": "Find My",  "mac": "4E:11:A0:3C:97:22", "rssi": -52, "age": 0},
    {"label": "Tile",     "mac": "E2:0C:7B:44:19:83", "rssi": -67, "age": 2},
    {"label": "Find My",  "mac": "56:9D:2F:08:B1:6E", "rssi": -74, "age": 1},
    {"label": "Samsung (SmartTag?)", "mac": "7A:31:C4:5D:02:AF",
     "rssi": -81, "age": 6},
    {"label": "Eddystone beacon", "mac": "62:88:EE:13:40:D7",
     "rssi": -89, "age": 14},
]

HUNT_ROW_H = 22
HUNT_SEL_BG = rgb(0x2124)


def render_hunt_pick(t):
    """drawPicker() in TrackerHunt.cpp."""
    t.fill_screen(BLACK)
    status_bar(t)

    top = 22
    bottom = H
    rows = (bottom - top - 18) // HUNT_ROW_H
    sel_index = 0

    t.fill_rect(0, top, W, bottom - top, BLACK)
    t.print_f1(8, top + 2, "trackers in range: %d" % len(HUNT_TARGETS),
               WHITE, BLACK)

    y = top + 18
    for i, d in enumerate(HUNT_TARGETS[:rows]):
        sel = i == sel_index
        bg = HUNT_SEL_BG if sel else BLACK
        if sel:
            t.fill_rect(0, y, W, HUNT_ROW_H, HUNT_SEL_BG)
        t.print_f1(8, y + 2, d["label"], UI_ICON if sel else WHITE, bg)
        t.print_f1(8, y + 12, d["mac"], UI_ICON if sel else DARKGREY, bg)
        t.print_f1(W - 96, y + 6, "%4d dBm  %2ds" % (d["rssi"], d["age"]),
                   UI_ICON if sel else WHITE, bg)
        y += HUNT_ROW_H


# The gauge's own constants, from TrackerHunt.cpp.
HUNT_RSSI_FAR = -100
HUNT_RSSI_NEAR = -35


def _dial():
    top, bottom = 30, H
    by_w = W // 2 - 10
    by_h = bottom - top - 90
    r = by_w if by_w < by_h else by_h
    return W // 2, top + r, r


def _angle_for(rssi):
    rssi = max(HUNT_RSSI_FAR, min(HUNT_RSSI_NEAR, rssi))
    frac = (rssi - HUNT_RSSI_FAR) / float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR)
    return int(180.0 - frac * 180.0 + 0.5)


def _polar(cx, cy, deg, radius):
    a = math.radians(deg)
    return (cx + int(math.cos(a) * radius + 0.5),
            cy - int(math.sin(a) * radius + 0.5))


def render_hunt_gauge(t):
    """drawGauge() in TrackerHunt.cpp, locked on the strongest row."""
    lock = HUNT_TARGETS[0]
    smooth = -47          # a few seconds of walking toward it
    peak = -45
    last = -49
    seen = 143
    trend = 1             # WARMER

    cx, cy, r = _dial()
    t.fill_screen(BLACK)
    status_bar(t)

    # drawGaugeChrome()
    t.fill_rect(0, 22, W, H - 22, BLACK)
    t.print_f1(8, 24, lock["label"], UI_ICON, BLACK)
    t.print_f1(W - 104, 24, lock["mac"], DARKGREY, BLACK)

    for deg in range(0, 181, 2):
        x, y = _polar(cx, cy, deg, r)
        col = RED if deg <= 40 else (UI_ICON if deg <= 80 else DARKGREY)
        t.draw_pixel(x, y, col)
    for deg in range(0, 181, 30):
        x0, y0 = _polar(cx, cy, deg, r)
        x1, y1 = _polar(cx, cy, deg, r - 8)
        t.draw_line(x0, y0, x1, y1, DARKGREY)
    t.print_f1(cx - r, cy + 2, "far", DARKGREY, BLACK)
    t.print_f1(cx + r - 22, cy + 2, "near", DARKGREY, BLACK)
    t.fill_circle(cx, cy, 3, DARKGREY)

    # peak marker, then the needle
    pdeg = _angle_for(peak)
    px0, py0 = _polar(cx, cy, pdeg, r)
    px1, py1 = _polar(cx, cy, pdeg, r - 12)
    t.draw_line(px0, py0, px1, py1, GREEN)

    deg = _angle_for(smooth)
    tx, ty_ = _polar(cx, cy, deg, r - 10)
    bx0, by0 = _polar(cx, cy, (deg + 90) % 360, 5)
    bx1, by1 = _polar(cx, cy, (deg + 270) % 360, 5)
    t.fill_triangle(tx, ty_, bx0, by0, bx1, by1, RED)

    # the readout
    ty = cy + 14
    t.fill_rect(0, ty, W, H - ty, BLACK)
    word, wcol = ("WARMER", GREEN) if trend > 0 else (
        ("COLDER", BLUE) if trend < 0 else ("HOLD", DARKGREY))
    t.centre_f1(word, cx, ty, wcol, BLACK, 3)

    sm = smooth
    if sm < -85:
        band, bcol = "FAR", DARKGREY
    elif sm < -70:
        band, bcol = "CLOSER", WHITE
    elif sm < -55:
        band, bcol = "NEAR", UI_ICON
    elif sm < -45:
        band, bcol = "VERY CLOSE", UI_ICON
    else:
        band, bcol = "ARM'S LENGTH", RED
    t.centre_f1(band, cx, ty + 30, bcol, BLACK, 2)

    bw, bx, by = W - 40, 20, ty + 54
    fill = int((sm - HUNT_RSSI_FAR) / float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR) * bw)
    fill = max(0, min(bw, fill))
    t.draw_rect(bx, by, bw, 10, DARKGREY)
    t.fill_rect(bx + 1, by + 1, fill, 8, bcol)
    ppx = bx + int((peak - HUNT_RSSI_FAR) /
                   float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR) * bw)
    t.draw_fast_vline(max(bx, min(bx + bw, ppx)), by - 3, 16, GREEN)

    t.centre_f1("%d dBm    best %d    %d seen" % (sm, peak, seen),
                cx, by + 18, DARKGREY, BLACK, 1)


# ── Fast Pair ──────────────────────────────────────────────────────────────
#
# Replays FastPairScan.cpp's drawHeader() and drawList(). Three lines per
# device at kRowH = 30, the same shape as Spotter's list.
#
# Colour is the frame type and nothing else: green is a device advertising a
# Model ID, which is what pairing mode looks like; cyan is an account-key
# frame, meaning it already belongs to somebody; grey is an empty filter.
#
# Addresses and model IDs are invented. Every Model ID row shows the raw
# hex, because that is what the device does: kModels in FastPair.cpp is a
# sentinel and nothing else, on the grounds that a name needs a source
# rather than a spam list, so modelName() returns nullptr for everything.
# A render showing "Pixel Buds Pro" would be inventing a capability.

FASTPAIR_DEVS = [
    {"line1": "PAIRING  model 0E30A0", "col": GREEN,
     "mac": "F0:9E:4A:22:8B:01", "addr": "pub", "rssi": -44,
     "line3": "batt 90/85/60+ x37 22s", "sel": True},
    {"line1": "paired   filter 6 bytes", "col": CYAN,
     "mac": "5C:3A:11:9D:74:E2", "addr": "rnd", "rssi": -61,
     "line3": "x214 4m", "sel": False},
    {"line1": "PAIRING  model 92BBBD", "col": GREEN,
     "mac": "A4:C1:38:0B:66:1F", "addr": "pub", "rssi": -72,
     "line3": "x12 9s", "sel": False},
    {"line1": "paired   no account keys", "col": LIGHTGREY,
     "mac": "6E:82:D5:40:AA:3C", "addr": "rnd", "rssi": -79,
     "line3": "x88 11m", "sel": False},
    {"line1": "paired   filter 10 bytes", "col": CYAN,
     "mac": "72:0D:9C:57:31:B8", "addr": "rnd", "rssi": -86,
     "line3": "x5 1h", "sel": False},
]

FASTPAIR_SEL_BG = rgb(0x18E3)


def render_fastpair(t):
    """drawHeader() and drawList() in FastPairScan.cpp."""
    t.fill_screen(BLACK)
    status_bar(t)

    t.fill_rect(0, 20, W, 18, BLACK)
    t.print_f1(8, 24, "devices %d   adverts 1962" % len(FASTPAIR_DEVS),
               WHITE, BLACK)

    top, row_h = 42, 30
    rows = (H - top) // row_h
    t.fill_rect(0, top, W, H - top, BLACK)

    for i, d in enumerate(FASTPAIR_DEVS[:rows]):
        y = top + i * row_h
        bg = FASTPAIR_SEL_BG if d["sel"] else BLACK
        if d["sel"]:
            t.fill_rect(0, y - 2, W, row_h - 2, FASTPAIR_SEL_BG)
        t.print_f1(8, y, d["line1"], d["col"], bg)
        t.print_f1(8, y + 11, "%s %s %ddBm"
                   % (d["mac"], d["addr"], d["rssi"]), LIGHTGREY, bg)
        t.print_f1(8, y + 21, d["line3"], DARKGREY, bg)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "render"))
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--panel", type=int, choices=(28, 35), default=35,
                    help="which CYD panel to draw for (default: 35)")
    args = ap.parse_args()
    set_panel(args.panel)

    for p in (ICON_H, os.path.join(FONTS, "glcdfont.c")):
        if not os.path.isfile(p):
            print("missing %s - run tools/build.sh setup first" % p,
                  file=sys.stderr)
            return 1

    bitmaps = load_bitmaps()
    glcd = load_glcd()
    fw, fg = load_font16()
    brand = load_branding()
    global BRAND
    BRAND = brand
    print("loaded %d bitmaps, %d glcd chars, %d font-2 glyphs"
          % (len(bitmaps), len(glcd), len(fg)))
    print("branding: %s %s, by %s"
          % ("Pueo", brand["PUEO_VERSION"], brand["PUEO_AUTHOR"]))
    print("panel: %.1f\" -- %dx%d, %dx%d tiles"
          % (args.panel / 10, W, H, TILE_W, TILE_H))

    os.makedirs(args.out, exist_ok=True)
    for name, fn in (("boot", lambda t: render_boot(t, brand)),
                     ("menu", render_menu),
                     ("bluetooth", render_bluetooth),
                     ("spotter", render_spotter),
                     ("hunt-pick", render_hunt_pick),
                     ("hunt-gauge", render_hunt_gauge),
                     ("fastpair", render_fastpair),
                     ("hunt-mark",
                      lambda t: render_mark(t, "bitmap_pueo_hunt", "Hunt")),
                     ("spotter-mark",
                      lambda t: render_mark(t, "bitmap_pueo_spotter", "Spotter"))):
        t = Tft(glcd, fw, fg, bitmaps)
        fn(t)
        p1 = os.path.join(args.out, "pueo-screen-%s.png" % name)
        t.im.save(p1)
        big = t.im.resize((W * args.scale, H * args.scale), Image.NEAREST)
        pN = os.path.join(args.out, "pueo-screen-%s@%dx.png" % (name, args.scale))
        big.save(pN)
        print("  %-10s %s  and  %s" % (name, os.path.basename(p1),
                                       os.path.basename(pN)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
