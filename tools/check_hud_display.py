#!/usr/bin/env python3
"""The esp_lcd display setup matches the NM-CYD-C5 panel and runs fast.

Driven via esp_lcd (the Arduino SPI libs could not clock the C5's SPI). Pin the
ST7789 panel, the bus pins (SCLK 6 / MOSI 7 / DC 24 / CS 23), the backlight 25,
a 40 MHz pixel clock and DMA (SPI_DMA_CH_AUTO) -- that combination is what took
it from 7 to 32 FPS. Reads source; no board. (Pins from the vendor platformio.ini.)
"""
import re, sys
from pathlib import Path
I = Path(__file__).resolve().parent.parent / "hud" / "hud.ino"
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    s = I.read_text(encoding="utf-8", errors="replace")
    ok("ST7789 panel via esp_lcd", "esp_lcd_new_panel_st7789" in s)
    ok("SPI bus pins SCLK6/MOSI7", "LCD_SCLK 6" in s and "LCD_MOSI 7" in s)
    ok("DC24 / CS23", "LCD_DC   24" in s or "LCD_DC 24" in s)
    ok("CS 23", "LCD_CS   23" in s or "LCD_CS 23" in s)
    ok("backlight 25", "LCD_BL   25" in s or "LCD_BL 25" in s)
    ok("40 MHz pixel clock", re.search(r"LCD_PCLK\s*\(40\s*\*\s*1000\s*\*\s*1000\)", s) is not None)
    ok("DMA enabled (SPI_DMA_CH_AUTO)", "SPI_DMA_CH_AUTO" in s)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
