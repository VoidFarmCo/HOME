#pragma once
#include <stdint.h>

// The HUD's five pages (modes). The order is the tab order along the top strip.
enum HudMode { M_SCAN = 0, M_RADAR, M_MAP, M_COMMS, M_ENGAGE, M_COUNT };

void        hud_mode_set(int m);
int         hud_mode_get();
const char* hud_mode_name(int m);        // "SCAN" / "RADAR" / ...
uint16_t    hud_mode_accent(int m);      // per-page accent colour

// Temporary: auto-advance the mode every few seconds, until touch is wired.
void hud_mode_auto(uint32_t now_ms);

// Draw the given page (chrome + content) into the framebuffer for this frame.
void hud_page_draw(int mode, uint32_t now_ms);

// A tap at screen (x,y): if it lands on a top tab, switch to that page and take
// over from the auto-cycle.
void hud_on_touch(int x, int y);

