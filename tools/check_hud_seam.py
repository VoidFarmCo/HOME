#!/usr/bin/env python3
"""The present seam is really wired (esp_lcd), and setup brings the panel up.

A weak no-op hud_present_fb links but draws nothing; the strong override must
push the buffer via esp_lcd DMA (esp_lcd_panel_draw_bitmap). setup() must init
the panel (lcd_init -> esp_lcd_new_panel_st7789), drive the backlight, alloc the
framebuffer and report a failure, and loop() must tick. Reads source; the real
proof is the owner's FPS reading on COM5.
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
    ok("strong hud_present_fb defined (not weak)",
       re.search(r"void\s+hud_present_fb\s*\([^)]*\)\s*\{", s) is not None
       and "__attribute__((weak))" not in s)
    ok("seam pushes the framebuffer via esp_lcd DMA",
       "esp_lcd_panel_draw_bitmap" in s)
    ok("panel brought up (esp_lcd ST7789 + init)",
       "esp_lcd_new_panel_st7789" in s and "esp_lcd_panel_init" in s)
    ok("backlight pin 25 driven high",
       re.search(r"digitalWrite\(\s*LCD_BL\s*,\s*HIGH\s*\)", s) is not None
       and re.search(r"#define\s+LCD_BL\s+25", s) is not None)
    ok("hud_init called + alloc-fail reported", "hud_init()" in s and "alloc failed" in s)
    ok("loop ticks the HUD", "hud_tick(millis())" in s)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
