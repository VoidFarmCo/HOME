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

// A fresh press at (x,y): bottom tab = switch page; on SCAN, the content band is
// split into three touch zones -- upper = scroll the cursor up, lower (above the
// tabs) = scroll down, centre = ENTER (lock the highlighted network, go to HUNT).
void hud_on_press(int x, int y);
// Auto-repeat while the finger is held: only the up/down scroll zones repeat
// (ENTER and the tabs do not auto-fire).
void hud_on_repeat(int x, int y);

