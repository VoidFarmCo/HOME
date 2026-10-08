#pragma once
#include <stdint.h>

// ============================================================
//  Combat HUD render core (ESP32-C5). Owns a DMA framebuffer,
//  composites into it, pushes once per frame via one seam.
//  hud_core = framebuffer + drawing API; hud_pages = the HUD content.
// ============================================================

#ifndef HUD_W
#define HUD_W 240      // logical width (portrait / vertical)
#endif
#ifndef HUD_H
#define HUD_H 320      // logical height (portrait / vertical)
#endif
#ifndef HUD_CAP_FPS
#define HUD_CAP_FPS 0  // 0 = uncapped
#endif

// RGB565 helper. Bytes are pre-swapped (MSB-first) because the ST7789 over
// esp_lcd expects swapped RGB565; doing it here is uniform and free per frame.
static inline uint16_t hud_rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c << 8) | (c >> 8));
}

// ---- shared palette ----
#define HUD_C_BG     hud_rgb(7,10,7)
#define HUD_C_STRIP  hud_rgb(14,20,14)
#define HUD_C_GREEN  hud_rgb(77,255,47)
#define HUD_C_DGREEN hud_rgb(22,59,22)
#define HUD_C_CYAN   hud_rgb(40,220,220)
#define HUD_C_AMBER  hud_rgb(255,176,0)
#define HUD_C_RED    hud_rgb(255,59,59)
#define HUD_C_GREY   hud_rgb(120,130,120)
#define HUD_C_WHITE  hud_rgb(230,235,230)

// ---- lifecycle ----
bool            hud_init();                // allocate the framebuffer. false = OOM
void            hud_tick(uint32_t now_ms); // call every loop(): composite + present
uint16_t        hud_fps_x10();             // last measured FPS * 10
const uint16_t* hud_framebuffer();

// ---- drawing API (valid after hud_init(); draws into the framebuffer) ----
void hud_clear(uint16_t c);
void hud_px(int x, int y, uint16_t c);
void hud_fill_rect(int x, int y, int w, int h, uint16_t c);
void hud_rect(int x, int y, int w, int h, uint16_t c);      // outline
void hud_line(int x0, int y0, int x1, int y1, uint16_t c);
void hud_ring(int cx, int cy, int r, uint16_t c);
void hud_disc(int cx, int cy, int r, uint16_t c);
void hud_num(int x, int y, uint32_t v, int scale, uint16_t c);
// 5x7 uppercase/digit/symbol text. Returns the x just past the string.
int  hud_text(int x, int y, const char* s, int scale, uint16_t c);
int  hud_text_w(const char* s, int scale);

// Set the current touch point (screen coords) so hud_tick draws a marker there;
// pass y < 0 for "not touched".
void hud_set_touch(int x, int y);

// ---- integration seam (implemented once in the sketch) ----
void hud_present_fb(const uint16_t* fb, int w, int h);
// Re-run the guided touch calibration (settings gear -> RECALIBRATE). The sketch
// owns the cal state, so this is a seam it implements.
void hud_request_recal();
