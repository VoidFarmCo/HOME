#include "hud_core.h"
#include <math.h>
#include <stdlib.h>
#include "esp_heap_caps.h"

// ---- state ----
static uint16_t* s_fb      = nullptr;
static uint16_t  s_fps_x10 = 0;
static uint32_t  s_frames  = 0;
static uint32_t  s_fps_t0  = 0;
static float     s_sweep   = 0.0f;

// Weak default so the firmware links before the display seam is written.
// Define a strong hud_present_fb() in your adapter to override this.
void __attribute__((weak)) hud_present_fb(const uint16_t*, int, int) {}

// ---- framebuffer primitives (all draw into s_fb) ----
static inline void px(int x, int y, uint16_t c) {
  if ((unsigned)x < HUD_W && (unsigned)y < HUD_H) s_fb[y * HUD_W + x] = c;
}
static void fillRect(int x, int y, int w, int h, uint16_t c) {
  for (int j = 0; j < h; j++) {
    int yy = y + j; if ((unsigned)yy >= HUD_H) continue;
    uint16_t* row = &s_fb[yy * HUD_W];
    for (int i = 0; i < w; i++) { int xx = x + i; if ((unsigned)xx < HUD_W) row[xx] = c; }
  }
}
static void clearFb(uint16_t c) { for (int i = 0; i < HUD_W * HUD_H; i++) s_fb[i] = c; }
static void line(int x0, int y0, int x1, int y1, uint16_t c) {
  int dx = abs(x1 - x0), sx = x0 < x1 ? 1 : -1;
  int dy = -abs(y1 - y0), sy = y0 < y1 ? 1 : -1;
  int err = dx + dy, e2;
  for (;;) {
    px(x0, y0, c);
    if (x0 == x1 && y0 == y1) break;
    e2 = 2 * err;
    if (e2 >= dy) { err += dy; x0 += sx; }
    if (e2 <= dx) { err += dx; y0 += sy; }
  }
}
static void ring(int cx, int cy, int r, uint16_t c) {
  int x = r, y = 0, d = 1 - r;
  while (x >= y) {
    px(cx + x, cy + y, c); px(cx + y, cy + x, c); px(cx - y, cy + x, c); px(cx - x, cy + y, c);
    px(cx - x, cy - y, c); px(cx - y, cy - x, c); px(cx + y, cy - x, c); px(cx + x, cy - y, c);
    y++;
    if (d < 0) d += 2 * y + 1; else { x--; d += 2 * (y - x) + 1; }
  }
}
static void disc(int cx, int cy, int r, uint16_t c) {
  for (int j = -r; j <= r; j++)
    for (int i = -r; i <= r; i++)
      if (i * i + j * j <= r * r) px(cx + i, cy + j, c);
}

// tiny 3x5 digit font (0-9) for the FPS readout
static const uint8_t F35[10][5] = {
  {7,5,5,5,7},{2,6,2,2,7},{7,1,7,4,7},{7,1,7,1,7},{5,5,7,1,1},
  {7,4,7,1,7},{7,4,7,5,7},{7,1,2,2,2},{7,5,7,5,7},{7,5,7,1,7}
};
static void glyph(int x, int y, int d, int s, uint16_t c) {
  for (int r = 0; r < 5; r++)
    for (int col = 0; col < 3; col++)
      if (F35[d][r] & (1 << (2 - col))) fillRect(x + col * s, y + r * s, s, s, c);
}
static void drawNum(int x, int y, uint32_t v, int s, uint16_t c) {
  char b[12]; int n = 0;
  if (v == 0) b[n++] = 0; else while (v) { b[n++] = v % 10; v /= 10; }
  for (int i = n - 1; i >= 0; i--) { glyph(x, y, b[i], s, c); x += 4 * s; }
}

// ---- palette ----
#define C_BG     hud_rgb(7,10,7)
#define C_STRIP  hud_rgb(14,20,14)
#define C_GREEN  hud_rgb(77,255,47)
#define C_DGREEN hud_rgb(22,59,22)
#define C_AMBER  hud_rgb(255,176,0)
#define C_RED    hud_rgb(255,59,59)

bool hud_init() {
  size_t bytes = (size_t)HUD_W * HUD_H * 2;
  // DMA-capable internal RAM first (esp_lcd streams it over SPI DMA); fall back
  // to PSRAM, then plain malloc.
  s_fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_DMA);
  if (!s_fb) s_fb = (uint16_t*)heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM);
  if (!s_fb) s_fb = (uint16_t*)malloc(bytes);
  if (!s_fb) return false;
  clearFb(C_BG);
  s_fps_t0 = 0; s_frames = 0;
  return true;
}
const uint16_t* hud_framebuffer() { return s_fb; }
uint16_t        hud_fps_x10()     { return s_fps_x10; }

// V1 demo scene: its only job is to measure real framerate on YOUR panel.
// Replace this body with the real widgets in step 2 (radar/map/strip/bar).
void hud_tick(uint32_t now) {
  if (!s_fb) return;
#if HUD_CAP_FPS > 0
  static uint32_t last = 0; uint32_t mindt = 1000 / HUD_CAP_FPS;
  if (now - last < mindt) return; last = now;
#endif

  clearFb(C_BG);
  fillRect(0, 0, HUD_W, 22, C_STRIP);          // top status strip
  fillRect(6, 8, 6, 6, C_GREEN);               // mode dot

  // Radar scope, sized to sit between the top strip (ends y=22) and bottom bar
  // (starts y=HUD_H-26) so nothing is clipped.
  int cx = HUD_W / 3, cy = (22 + (HUD_H - 26)) / 2, R = (HUD_H - 26 - 22) / 2 - 6;
  ring(cx, cy, R, C_DGREEN); ring(cx, cy, R * 2 / 3, C_DGREEN); ring(cx, cy, R / 3, C_DGREEN);

  s_sweep += 0.06f; if (s_sweep > 6.2832f) s_sweep -= 6.2832f; // rotating sweep
  line(cx, cy, cx + (int)(cosf(s_sweep) * R), cy + (int)(sinf(s_sweep) * R), C_GREEN);

  int pr = 3 + (int)(2 * (0.5f + 0.5f * sinf(now * 0.006f)));   // pulsing alert blip
  disc(cx + R / 2, cy - R / 3, pr, C_RED);
  disc(cx - R / 3, cy + R / 4, 3, C_AMBER);
  disc(cx, cy, 2, C_GREEN);                                    // you

  drawNum(HUD_W - 90, 28, s_fps_x10 / 10, 4, C_GREEN);         // FPS (integer)
  fillRect(0, HUD_H - 26, HUD_W, 26, C_STRIP);                 // bottom bar

  hud_present_fb(s_fb, HUD_W, HUD_H);                          // <-- the one seam

  s_frames++;
  if (s_fps_t0 == 0) s_fps_t0 = now;
  if (now - s_fps_t0 >= 1000) {
    s_fps_x10 = (uint16_t)(s_frames * 10000 / (now - s_fps_t0));
    s_frames = 0; s_fps_t0 = now;
  }
}
