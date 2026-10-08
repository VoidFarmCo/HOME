#include "hud_pages.h"
#include "hud_core.h"
#include <math.h>

static int s_mode = M_RADAR;

void hud_mode_set(int m) { if (m >= 0 && m < M_COUNT) s_mode = m; }
int  hud_mode_get()      { return s_mode; }

const char* hud_mode_name(int m) {
  switch (m) {
    case M_SCAN:   return "SCAN";
    case M_RADAR:  return "RADAR";
    case M_MAP:    return "MAP";
    case M_COMMS:  return "COMMS";
    case M_ENGAGE: return "ENGAGE";
    default:       return "?";
  }
}
uint16_t hud_mode_accent(int m) {
  switch (m) {
    case M_SCAN:   return HUD_C_CYAN;
    case M_RADAR:  return HUD_C_GREEN;
    case M_MAP:    return HUD_C_GREEN;
    case M_COMMS:  return HUD_C_AMBER;
    case M_ENGAGE: return HUD_C_RED;
    default:       return HUD_C_WHITE;
  }
}

// Temporary auto-cycle (every 4 s) so all pages can be seen before touch exists.
void hud_mode_auto(uint32_t now) {
  static uint32_t last = 0;
  if (last == 0) last = now;
  if (now - last >= 4000) { s_mode = (s_mode + 1) % M_COUNT; last = now; }
}

// ---- layout constants ----
#define TOP_H     28
#define BOT_H     26
#define CONTENT_Y (TOP_H + 2)
#define CONTENT_B (HUD_H - BOT_H - 2)   // bottom of the content band

// Short tab labels so they're readable at 2x in a 64 px tab.
static const char* tab_label(int m) {
  switch (m) {
    case M_SCAN:   return "SCAN";
    case M_RADAR:  return "RADR";
    case M_MAP:    return "MAP";
    case M_COMMS:  return "COMS";
    case M_ENGAGE: return "ENG";
    default:       return "?";
  }
}

// ---- top tab strip + bottom status bar (shared chrome) ----
static void draw_chrome(int mode) {
  hud_fill_rect(0, 0, HUD_W, TOP_H, HUD_C_STRIP);
  const int tabW = HUD_W / M_COUNT;
  const int ty = (TOP_H - 14) / 2;              // 14 = 7-row font at scale 2
  for (int i = 0; i < M_COUNT; i++) {
    int x = i * tabW;
    uint16_t acc = hud_mode_accent(i);
    const char* lbl = tab_label(i);
    int tx = x + (tabW - hud_text_w(lbl, 2)) / 2;
    if (i == mode) {
      hud_fill_rect(x + 1, 1, tabW - 2, TOP_H - 2, acc);
      hud_text(tx, ty, lbl, 2, HUD_C_BG);        // dark text on the accent fill
    } else {
      hud_text(tx, ty, lbl, 2, acc);             // accent-coloured text on dark (readable + colour-coded)
    }
    if (i) hud_fill_rect(x, 2, 1, TOP_H - 4, HUD_C_BG);   // tab divider
  }
  hud_fill_rect(0, HUD_H - BOT_H, HUD_W, BOT_H, HUD_C_STRIP);
}

// ---- RADAR ----
static void page_radar(uint32_t now) {
  static float sweep = 0.0f;
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2, R = (CONTENT_B - CONTENT_Y) / 2 - 4;
  hud_ring(cx, cy, R, HUD_C_DGREEN); hud_ring(cx, cy, R * 2 / 3, HUD_C_DGREEN); hud_ring(cx, cy, R / 3, HUD_C_DGREEN);
  hud_line(cx - R, cy, cx + R, cy, HUD_C_DGREEN); hud_line(cx, cy - R, cx, cy + R, HUD_C_DGREEN);
  sweep += 0.07f; if (sweep > 6.2832f) sweep -= 6.2832f;
  hud_line(cx, cy, cx + (int)(cosf(sweep) * R), cy + (int)(sinf(sweep) * R), HUD_C_GREEN);
  int pr = 3 + (int)(2 * (0.5f + 0.5f * sinf(now * 0.006f)));
  hud_disc(cx + R / 2, cy - R / 3, pr, HUD_C_RED);
  hud_disc(cx - R / 3, cy + R / 4, 3, HUD_C_AMBER);
  hud_disc(cx + R / 4, cy + R / 2, 3, HUD_C_GREEN);
  hud_disc(cx, cy, 2, HUD_C_WHITE);
  hud_text(8, HUD_H - BOT_H + 9, "CONTACTS 3", 1, HUD_C_GREEN);
}

// ---- SCAN (recon list placeholder) ----
static void page_scan(uint32_t now) {
  static const char* names[6] = {"AP-NETGEAR", "IPHONE-7F", "ESP-A91C", "TPLINK_22", "HIDDEN", "PRINTER-9"};
  int y = CONTENT_Y + 2;
  for (int i = 0; i < 6; i++) {
    hud_text(8, y + 2, names[i], 1, HUD_C_WHITE);
    int rssi = 20 + (int)(70 * (0.5f + 0.5f * sinf(now * 0.002f + i)));   // animated bar
    hud_fill_rect(HUD_W - 10 - rssi, y + 2, rssi, 8, (i == 0) ? HUD_C_CYAN : HUD_C_DGREEN);
    hud_num(HUD_W - 10 - rssi - 26, y + 1, rssi, 1, HUD_C_GREY);
    y += 20;
  }
  hud_text(8, HUD_H - BOT_H + 9, "SCANNING  6 SEEN", 1, HUD_C_CYAN);
}

// ---- MAP (grid + you + contacts) ----
static void page_map(uint32_t now) {
  for (int gx = 0; gx < HUD_W; gx += 32) hud_fill_rect(gx, CONTENT_Y, 1, CONTENT_B - CONTENT_Y, HUD_C_DGREEN);
  for (int gy = CONTENT_Y; gy < CONTENT_B; gy += 32) hud_fill_rect(0, gy, HUD_W, 1, HUD_C_DGREEN);
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2;
  hud_line(cx - 7, cy, cx + 7, cy, HUD_C_GREEN); hud_line(cx, cy - 7, cx, cy + 7, HUD_C_GREEN);
  hud_ring(cx, cy, 5, HUD_C_GREEN);
  float a = now * 0.0012f;
  hud_disc(cx + (int)(cosf(a) * 60), cy + (int)(sinf(a) * 40), 3, HUD_C_AMBER);
  hud_disc(cx + (int)(cosf(a * 1.7f) * 90), cy + (int)(sinf(a * 1.7f) * 55), 3, HUD_C_RED);
  hud_text(cx + 10, CONTENT_Y + 2, "N", 1, HUD_C_WHITE);
  hud_text(8, HUD_H - BOT_H + 9, "GPS 7 SAT", 1, HUD_C_GREEN);
}

// ---- COMMS (chat placeholder) ----
static void page_comms(uint32_t now) {
  static const char* msgs[4] = {"OVERWATCH: HOLD", "YOU: COPY", "ALPHA: MOVING E", "YOU: ON IT"};
  int y = CONTENT_Y + 4;
  for (int i = 0; i < 4; i++) {
    bool me = (i % 2) == 1;
    int tw = hud_text_w(msgs[i], 1);
    int x = me ? (HUD_W - 12 - tw) : 12;
    hud_fill_rect(x - 3, y - 2, tw + 6, 12, me ? HUD_C_DGREEN : HUD_C_STRIP);
    hud_text(x, y, msgs[i], 1, me ? HUD_C_GREEN : HUD_C_AMBER);
    y += 18;
  }
  if ((now / 500) & 1) hud_fill_rect(12, y + 2, 6, 10, HUD_C_AMBER);     // blinking cursor
  hud_text(8, HUD_H - BOT_H + 9, "COMMS  ONLINE", 1, HUD_C_AMBER);
}

// ---- ENGAGE (targeting) ----
static void page_engage(uint32_t now) {
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2;
  int b = 60;                                             // bracket half-size
  // corner brackets
  int bl = 16;
  hud_line(cx - b, cy - b, cx - b + bl, cy - b, HUD_C_RED); hud_line(cx - b, cy - b, cx - b, cy - b + bl, HUD_C_RED);
  hud_line(cx + b, cy - b, cx + b - bl, cy - b, HUD_C_RED); hud_line(cx + b, cy - b, cx + b, cy - b + bl, HUD_C_RED);
  hud_line(cx - b, cy + b, cx - b + bl, cy + b, HUD_C_RED); hud_line(cx - b, cy + b, cx - b, cy + b - bl, HUD_C_RED);
  hud_line(cx + b, cy + b, cx + b - bl, cy + b, HUD_C_RED); hud_line(cx + b, cy + b, cx + b, cy + b - bl, HUD_C_RED);
  // crosshair + pulsing target
  hud_line(cx - 12, cy, cx + 12, cy, HUD_C_RED); hud_line(cx, cy - 12, cx, cy + 12, HUD_C_RED);
  int pr = 6 + (int)(4 * (0.5f + 0.5f * sinf(now * 0.008f)));
  hud_ring(cx, cy, pr, HUD_C_AMBER);
  // arming bar
  int w = (now / 20) % (HUD_W - 40);
  hud_rect(20, CONTENT_B - 14, HUD_W - 40, 10, HUD_C_RED);
  hud_fill_rect(22, CONTENT_B - 12, w, 6, HUD_C_RED);
  hud_text(cx - hud_text_w("TGT LOCK", 1) / 2, CONTENT_Y + 2, "TGT LOCK", 1, HUD_C_RED);
  hud_text(8, HUD_H - BOT_H + 9, "ARMED", 1, HUD_C_RED);
}

void hud_page_draw(int mode, uint32_t now) {
  switch (mode) {
    case M_SCAN:   page_scan(now);   break;
    case M_RADAR:  page_radar(now);  break;
    case M_MAP:    page_map(now);    break;
    case M_COMMS:  page_comms(now);  break;
    case M_ENGAGE: page_engage(now); break;
    default: break;
  }
  draw_chrome(mode);   // chrome last so tabs/strips sit on top of content
}
