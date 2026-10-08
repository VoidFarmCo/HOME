#pragma once
#include <stdint.h>

// ============================================================
//  Combat HUD - V1 render core (ESP32-C5, single-core RISC-V)
//  Owns a PSRAM framebuffer, composites into it, pushes once.
//  Drop into your existing firmware; wire ONE seam (below).
// ============================================================

// ---- Config: match these to your panel + driver rotation ----
#ifndef HUD_W
#define HUD_W 320      // logical width  (landscape). Set your driver rotation to match.
#endif
#ifndef HUD_H
#define HUD_H 240      // logical height
#endif
#ifndef HUD_CAP_FPS
#define HUD_CAP_FPS 0  // 0 = uncapped (measure your ceiling). Set e.g. 30 to cap later.
#endif

// RGB565 helper. Bytes are pre-swapped (MSB-first) because the ST7789 over
// esp_lcd expects swapped RGB565; doing it here is uniform and free per frame.
static inline uint16_t hud_rgb(uint8_t r, uint8_t g, uint8_t b) {
  uint16_t c = (uint16_t)(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3));
  return (uint16_t)((c << 8) | (c >> 8));
}

// ---- Public API ----
bool            hud_init();                // allocate PSRAM framebuffer. false = OOM
void            hud_tick(uint32_t now_ms); // call every loop(): composite + present
uint16_t        hud_fps_x10();             // last measured FPS * 10
const uint16_t* hud_framebuffer();         // raw buffer if you need it

// ---- Integration seam ------------------------------------------------
// YOU implement this once, bridging to YOUR existing display driver.
// A weak no-op default is provided so the project links before you wire it.
// See INTEGRATION_R1.md for TFT_eSPI / LovyanGFX / esp_lcd examples.
void hud_present_fb(const uint16_t* fb, int w, int h);
