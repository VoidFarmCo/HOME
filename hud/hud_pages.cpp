#include "hud_pages.h"
#include "hud_core.h"
#include "hud_scan.h"
#include <math.h>
#include <stdio.h>
#include <string.h>

// Map a signal RSSI (dBm, ~-30 near .. ~-95 far) to 0..1 (1 = strongest).
static float rssi_unit(int8_t rssi) {
  float u = (rssi + 95) / 60.0f;          // -95 -> 0, -35 -> 1
  return u < 0 ? 0 : u > 1 ? 1 : u;
}
// Interim blip placement angle (NOT a real bearing -- true bearing needs the DF
// antenna + compass; this just spreads contacts so the scope isn't a single dot).
static float name_angle(const char* s) {
  uint32_t h = 2166136261u;
  for (const char* p = s; *p; p++) { h ^= (uint8_t)*p; h *= 16777619u; }
  return (h % 3600) * 0.0017453f;         // 0..2pi
}

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

static bool s_manual = false;   // once the user taps a tab, stop auto-cycling

// Auto-cycle (every 4 s) so all pages can be seen -- until the user takes over.
void hud_mode_auto(uint32_t now) {
  if (s_manual) return;
  static uint32_t last = 0;
  if (last == 0) last = now;
  if (now - last >= 4000) { s_mode = (s_mode + 1) % M_COUNT; last = now; }
}

// ---- layout constants (portrait: status strip on top, TAB STRIP ON THE BOTTOM) ----
#define STAT_H    26                    // top status strip (footer text + FPS)
#define TAB_H     28                    // bottom tab strip (the page switcher)
#define CONTENT_Y (STAT_H + 2)
#define CONTENT_B (HUD_H - TAB_H - 2)   // bottom of the content band
#define FOOT_Y    8                     // page footer text sits in the top strip now

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

// ---- top status strip + BOTTOM tab strip (shared chrome) ----
static void draw_chrome(int mode) {
  hud_fill_rect(0, 0, HUD_W, STAT_H, HUD_C_STRIP);           // top status strip
  const int tabY = HUD_H - TAB_H;                            // tabs along the bottom
  hud_fill_rect(0, tabY, HUD_W, TAB_H, HUD_C_STRIP);
  const int tabW = HUD_W / M_COUNT;
  const int ty = tabY + (TAB_H - 14) / 2;                    // 14 = 7-row font at scale 2
  for (int i = 0; i < M_COUNT; i++) {
    int x = i * tabW;
    uint16_t acc = hud_mode_accent(i);
    const char* lbl = tab_label(i);
    int tx = x + (tabW - hud_text_w(lbl, 2)) / 2;
    if (i == mode) {
      hud_fill_rect(x + 1, tabY + 1, tabW - 2, TAB_H - 2, acc);
      hud_text(tx, ty, lbl, 2, HUD_C_BG);        // dark text on the accent fill
    } else {
      hud_text(tx, ty, lbl, 2, acc);             // accent-coloured text on dark (readable + colour-coded)
    }
    if (i) hud_fill_rect(x, tabY + 2, 1, TAB_H - 4, HUD_C_BG);   // tab divider
  }
}

// ---- RADAR: detection scope for ALL signals, strong = near the centre ----
// NOTE: blip ANGLE is not a true bearing yet -- real direction (blips at their
// actual bearing, the scope rotating as you turn) needs the DF antenna + compass.
static void page_radar(uint32_t now) {
  static float sweep = 0.0f;
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2, R = (CONTENT_B - CONTENT_Y) / 2 - 4;
  hud_ring(cx, cy, R, HUD_C_DGREEN); hud_ring(cx, cy, R * 2 / 3, HUD_C_DGREEN); hud_ring(cx, cy, R / 3, HUD_C_DGREEN);
  hud_line(cx - R, cy, cx + R, cy, HUD_C_DGREEN); hud_line(cx, cy - R, cx, cy + R, HUD_C_DGREEN);
  sweep += 0.07f; if (sweep > 6.2832f) sweep -= 6.2832f;
  hud_line(cx, cy, cx + (int)(cosf(sweep) * R), cy + (int)(sinf(sweep) * R), HUD_C_GREEN);

  const Contact* c = hud_scan_list();
  int n = hud_scan_count();
  for (int i = 0; i < n; i++) {
    float u = rssi_unit(c[i].rssi);                 // 1 = strong/near
    int rr = (int)((1.0f - u) * R);                 // near centre when strong
    float a = name_angle(c[i].name);
    int bx = cx + (int)(cosf(a) * rr), by = cy + (int)(sinf(a) * rr);
    uint16_t col = (u > 0.66f) ? HUD_C_AMBER : (u > 0.33f ? HUD_C_GREEN : HUD_C_DGREEN);
    hud_disc(bx, by, 3, col);
  }
  hud_disc(cx, cy, 2, HUD_C_WHITE);                 // you

  char foot[48]; snprintf(foot, sizeof(foot), "CON %d  W%d B%d", n, hud_scan_wifi_count(), hud_scan_ble_count());
  hud_text(8, FOOT_Y, foot, 1, HUD_C_GREEN);
}

// ---- SCAN list cursor (highlighted row) + scroll window ----
static int s_sel = 0, s_scroll = 0;
#define ROW_H 20
static int scan_rows() { return (CONTENT_B - CONTENT_Y) / ROW_H; }

// Move the highlighted row by d, keeping it inside the scroll window.
static void scan_move(int d) {
  int n = hud_scan_count();
  if (n <= 0) return;
  s_sel += d;
  if (s_sel < 0) s_sel = 0;
  if (s_sel > n - 1) s_sel = n - 1;
  int rows = scan_rows();
  if (s_sel < s_scroll) s_scroll = s_sel;
  if (s_sel >= s_scroll + rows) s_scroll = s_sel - rows + 1;
}

// ---- SCAN (live): the real contact list, strongest first; cursor + scroll ----
static void page_scan(uint32_t now) {
  (void)now;
  const Contact* c = hud_scan_list();
  int n = hud_scan_count();
  if (s_sel > n - 1) s_sel = n > 0 ? n - 1 : 0;      // clamp as the list changes
  int rows = scan_rows();
  if (s_scroll > n - rows) s_scroll = n - rows > 0 ? n - rows : 0;

  if (n == 0) hud_text(8, CONTENT_Y + 8, "SCANNING...", 1, HUD_C_CYAN);

  for (int r = 0; r < rows; r++) {
    int i = s_scroll + r;
    if (i >= n) break;
    int y = CONTENT_Y + 2 + r * ROW_H;
    bool cur = (i == s_sel);
    bool tgt = hud_scan_has_target() && !strcmp(c[i].name, hud_scan_target_name());
    if (cur) hud_fill_rect(0, y - 1, HUD_W, ROW_H - 2, HUD_C_STRIP);  // highlight the cursor row
    hud_num(6, y + 1, c[i].rssi < 0 ? -c[i].rssi : c[i].rssi, 1, HUD_C_GREY);  // |dBm|
    hud_text(32, y + 2, c[i].name, 1, tgt ? HUD_C_RED : HUD_C_WHITE);
    int bar = (int)(rssi_unit(c[i].rssi) * 70);      // 0..70 px signal bar
    hud_fill_rect(HUD_W - 8 - bar, y + 2, bar, 8, cur ? HUD_C_CYAN : HUD_C_DGREEN);
  }
  // scroll-position hint: which block of the list is showing
  char foot[48];
  snprintf(foot, sizeof(foot), "%d/%d  UP/ENTER/DN", n ? s_sel + 1 : 0, n);
  hud_text(8, FOOT_Y, foot, 1, HUD_C_CYAN);
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
  hud_text(8, FOOT_Y, "GPS 7 SAT", 1, HUD_C_GREEN);
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
  hud_text(8, FOOT_Y, "COMMS  ONLINE", 1, HUD_C_AMBER);
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
  hud_text(8, FOOT_Y, "ARMED", 1, HUD_C_RED);
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

// Which scan zone is y in? -1 = scroll up, +1 = scroll down, 0 = centre/enter.
static int scan_zone(int y) {
  int band = CONTENT_B - CONTENT_Y;
  if (y < CONTENT_Y + band / 3) return -1;
  if (y > CONTENT_B - band / 3) return +1;
  return 0;
}

void hud_on_press(int x, int y) {
  if (y >= HUD_H - TAB_H) {                   // bottom tab strip = switch page
    int m = x / (HUD_W / M_COUNT);
    if (m >= 0 && m < M_COUNT) { hud_mode_set(m); s_manual = true; }
    return;
  }
  if (hud_mode_get() == M_SCAN) {
    int z = scan_zone(y);                      // upper = scroll up, lower = scroll down
    if (z == -1) scan_move(-1);
    else if (z == +1) scan_move(+1);
    // centre tap on SCAN is reserved for select-to-track (built with the real radar)
  }
}

void hud_on_repeat(int x, int y) {
  (void)x;
  if (y >= HUD_H - TAB_H) return;             // tabs/enter don't auto-repeat
  if (hud_mode_get() == M_SCAN) {
    int z = scan_zone(y);
    if (z == -1) scan_move(-1);
    else if (z == +1) scan_move(+1);
  }
}
