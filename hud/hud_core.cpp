#include "hud_core.h"
#include "hud_pages.h"
#include <math.h>
#include <stdlib.h>
#include <string.h>
#include "esp_heap_caps.h"

// ---- state ----
static uint16_t* s_fb      = nullptr;
static uint16_t  s_fps_x10 = 0;
static uint32_t  s_frames  = 0;
static uint32_t  s_fps_t0  = 0;

// Weak default so the firmware links before the display seam is written.
void __attribute__((weak)) hud_present_fb(const uint16_t*, int, int) {}

// ---- framebuffer drawing API (all draw into s_fb) ----
void hud_px(int x, int y, uint16_t c) {
  if ((unsigned)x < HUD_W && (unsigned)y < HUD_H) s_fb[y * HUD_W + x] = c;
}
void hud_fill_rect(int x, int y, int w, int h, uint16_t c) {
  for (int j = 0; j < h; j++) {
    int yy = y + j; if ((unsigned)yy >= HUD_H) continue;
    uint16_t* row = &s_fb[yy * HUD_W];
    for (int i = 0; i < w; i++) { int xx = x + i; if ((unsigned)xx < HUD_W) row[xx] = c; }
  }
}
void hud_rect(int x, int y, int w, int h, uint16_t c) {
  hud_fill_rect(x, y, w, 1, c); hud_fill_rect(x, y + h - 1, w, 1, c);
  hud_fill_rect(x, y, 1, h, c); hud_fill_rect(x + w - 1, y, 1, h, c);
}
void hud_clear(uint16_t c) { for (int i = 0; i < HUD_W * HUD_H; i++) s_fb[i] = c; }
void hud_line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;
  for (;;) {
    hud_px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
void hud_ring(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, d = 1 - r;
  while (x >= y) {
    hud_px(cx + x, cy + y, c); hud_px(cx + y, cy + x, c); hud_px(cx - y, cy + x, c); hud_px(cx - x, cy + y, c);
    hud_px(cx - x, cy - y, c); hud_px(cx - y, cy - x, c); hud_px(cx + y, cy - x, c); hud_px(cx + x, cy - y, c);
    y++;
    if (d < 0) d += 2 * y + 1; else { x--; d += 2 * (y - x) + 1; }
  }
}
void hud_disc(int cx, int cy, int r, uint16_t c) {
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r) hud_px(cx + i, cy + j, c);
}

// tiny 3x5 digit font (0-9) for the numeric readouts
static const uint8_t F35[10][5] = {
  {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
  {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}
};
static void glyph35(int x, int y, int d, int s, uint16_t c) {
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 3; col++)
      if (F35[d][r] & (1 << (2 - col))) hud_fill_rect(x + col * s, y + r * s, s, s, c);
}
void hud_num(int x, int y, uint32_t v, int s, uint16_t c) {
  char b[12]; int n = 0;
  if (v == 0) b[n++] = 0; else while (v) { b[n++] = v % 10; v /= 10; }
  for (int i = n - 1; i >= 0; i--) { glyph35(x, y, b[i], s, c); x += 4 * s; }
}

// 5x7 font (column-major, bit0 = top row), ASCII 0x20..0x5A. Classic set.
static const uint8_t FONT57[][5] = {
  {0,0,0,0,0},{0,0,0x5F,0,0},{0,7,0,7,0},{0x14,0x7F,0x14,0x7F,0x14},
  {0x24,0x2A,0x7F,0x2A,0x12},{0x23,0x13,8,0x64,0x62},{0x36,0x49,0x55,0x22,0x50},{0,5,3,0,0},
  {0,0x1C,0x22,0x41,0},{0,0x41,0x22,0x1C,0},{0x14,8,0x3E,8,0x14},{8,8,0x3E,8,8},
  {0,0x50,0x30,0,0},{8,8,8,8,8},{0,0x60,0x60,0,0},{0x20,0x10,8,4,2},
  {0x3E,0x51,0x49,0x45,0x3E},{0,0x42,0x7F,0x40,0},{0x42,0x61,0x51,0x49,0x46},{0x21,0x41,0x45,0x4B,0x31},
  {0x18,0x14,0x12,0x7F,0x10},{0x27,0x45,0x45,0x45,0x39},{0x3C,0x4A,0x49,0x49,0x30},{1,0x71,9,5,3},
  {0x36,0x49,0x49,0x49,0x36},{6,0x49,0x49,0x29,0x1E},{0,0x36,0x36,0,0},{0,0x56,0x36,0,0},
  {8,0x14,0x22,0x41,0},{0x14,0x14,0x14,0x14,0x14},{0,0x41,0x22,0x14,8},{2,1,0x51,9,6},
  {0x32,0x49,0x79,0x41,0x3E},{0x7E,0x11,0x11,0x11,0x7E},{0x7F,0x49,0x49,0x49,0x36},{0x3E,0x41,0x41,0x41,0x22},
  {0x7F,0x41,0x41,0x22,0x1C},{0x7F,0x49,0x49,0x49,0x41},{0x7F,9,9,9,1},{0x3E,0x41,0x49,0x49,0x7A},
  {0x7F,8,8,8,0x7F},{0,0x41,0x7F,0x41,0},{0x20,0x40,0x41,0x3F,1},{0x7F,8,0x14,0x22,0x41},
  {0x7F,0x40,0x40,0x40,0x40},{0x7F,2,0x0C,2,0x7F},{0x7F,4,8,0x10,0x7F},{0x3E,0x41,0x41,0x41,0x3E},
  {0x7F,9,9,9,6},{0x3E,0x41,0x51,0x21,0x5E},{0x7F,9,0x19,0x29,0x46},{0x46,0x49,0x49,0x49,0x31},
  {1,1,0x7F,1,1},{0x3F,0x40,0x40,0x40,0x3F},{0x1F,0x20,0x40,0x20,0x1F},{0x3F,0x40,0x38,0x40,0x3F},
  {0x63,0x14,8,0x14,0x63},{7,8,0x70,8,7},{0x61,0x51,0x49,0x45,0x43}
};
int hud_text(int x, int y, const char* str, int s, uint16_t c) {
  for (const char* p = str; *p; p++) {
    char ch = *p;
    if (ch >= 'a' && ch <= 'z') ch -= 32;          // uppercase only
    if (ch < 0x20 || ch > 0x5A) { x += 6 * s; continue; }
    const uint8_t* g = FONT57[ch - 0x20];
    for (int col = 0; col < 5; col++)
      for (int row = 0; row < 7; row++)
        if (g[col] & (1 << row)) hud_fill_rect(x + col * s, y + row * s, s, s, c);
    x += 6 * s;                                     // 5 cols + 1 space
  }
  return x;
}
int hud_text_w(const char* str, int s) { return (int)strlen(str) * 6 * s; }

bool hud_init() {
  size_t bytes = (size_t)HUD_W * HUD_H * 2;
  // DMA-capable internal RAM first (esp_lcd streams it over SPI DMA); fall back
  // to PSRAM, then plain malloc.
  s_fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  if (!s_fb) s_fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!s_fb) s_fb = (uint16_t*)malloc(bytes);
  if (!s_fb) return false;
  hud_clear(HUD_C_BG);
  s_fps_t0 = 0; s_frames = 0;
  return true;
}
const uint16_t* hud_framebuffer() { return s_fb; }
uint16_t        hud_fps_x10()     { return s_fps_x10; }

// Frame driver: clear, let the current page composite its scene, overlay the
// FPS, push once. The page content lives in hud_pages.cpp.
void hud_tick(uint32_t now) {
  if (!s_fb) return;
#if HUD_CAP_FPS > 0
  static uint32_t last = 0; uint32_t mindt = 1000 / HUD_CAP_FPS;
  if (now - last < mindt) return; last = now;
#endif

  hud_mode_auto(now);                      // temporary: auto-cycle until touch is wired
  hud_clear(HUD_C_BG);
  hud_page_draw(hud_mode_get(), now);      // the active page draws its chrome + content
  // FPS in the bottom-right status bar (out of the way of the top tabs).
  hud_text(HUD_W - 70, HUD_H - 16, "FPS", 1, HUD_C_GREY);
  hud_num(HUD_W - 42, HUD_H - 19, s_fps_x10 / 10, 2, HUD_C_GREEN);

  hud_present_fb(s_fb, HUD_W, HUD_H);       // the one seam

  s_frames++;
  if (s_fps_t0 == 0) s_fps_t0 = now;
  if (now - s_fps_t0 >= 1000) {
    s_fps_x10 = (uint16_t)(s_frames * 10000 / (now - s_fps_t0));
    s_frames = 0; s_fps_t0 = now;
  }
}
