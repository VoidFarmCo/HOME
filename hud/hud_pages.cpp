#include "hud_pages.h"
#include "hud_core.h"
#include "hud_scan.h"
#include "home_logo.h"
#include "branding.h"
#include <Arduino.h>
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

// settings gear (top-right of the top strip) + the SETTINGS panel
static bool s_settings = false;
static int  s_band = 0;              // band filter: 0 = ALL, 1 = 2.4 GHz, 2 = 5 GHz
static bool s_detail = false;        // network info view open
static int  s_detail_ci = -1;        // which contact index it shows
#define GEAR_X    (HUD_W - 13)
#define GEAR_Y    (STAT_H / 2)
#define SET_BTN_X 20
#define SET_BTN_Y (CONTENT_Y + 44)
#define SET_BTN_W (HUD_W - 40)
#define SET_BTN_H 30
#define SET_BND_Y (SET_BTN_Y + SET_BTN_H + 14)   // BAND filter row, below RECALIBRATE

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

  // settings gear, top-right of the status strip (amber when the panel is open)
  uint16_t gc = s_settings ? HUD_C_AMBER : HUD_C_GREY;
  for (int a = 0; a < 8; a++) {                               // cog teeth
    float ang = a * 0.785398f;
    hud_fill_rect(GEAR_X + (int)(cosf(ang) * 8) - 1, GEAR_Y + (int)(sinf(ang) * 8) - 1, 3, 3, gc);
  }
  hud_disc(GEAR_X, GEAR_Y, 6, gc);
  hud_disc(GEAR_X, GEAR_Y, 2, HUD_C_STRIP);                  // centre hole (strip colour)
}

// ---- SETTINGS panel (opened by the gear) ----
static void draw_settings() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  hud_text(HUD_W / 2 - hud_text_w("SETTINGS", 2) / 2, CONTENT_Y + 8, "SETTINGS", 2, HUD_C_WHITE);
  char ver[40]; snprintf(ver, sizeof(ver), "%s  v.%s", HUD_BUILD, HUD_VERSION);
  hud_text(HUD_W / 2 - hud_text_w(ver, 1) / 2, CONTENT_Y + 26, ver, 1, HUD_C_GREY);
  // RECALIBRATE TOUCH button
  hud_rect(SET_BTN_X, SET_BTN_Y, SET_BTN_W, SET_BTN_H, HUD_C_CYAN);
  const char* b = "RECALIBRATE TOUCH";
  hud_text(HUD_W / 2 - hud_text_w(b, 1) / 2, SET_BTN_Y + (SET_BTN_H - 7) / 2, b, 1, HUD_C_CYAN);
  // BAND filter toggle (ALL / 2.4 / 5 GHz)
  hud_rect(SET_BTN_X, SET_BND_Y, SET_BTN_W, SET_BTN_H, HUD_C_AMBER);
  char bl[24]; snprintf(bl, sizeof(bl), "BAND:  %s", s_band == 1 ? "2.4 GHZ" : s_band == 2 ? "5 GHZ" : "ALL");
  hud_text(HUD_W / 2 - hud_text_w(bl, 1) / 2, SET_BND_Y + (SET_BTN_H - 7) / 2, bl, 1, HUD_C_AMBER);
  hud_text(HUD_W / 2 - hud_text_w("TAP GEAR TO CLOSE", 1) / 2, CONTENT_B - 16, "TAP GEAR TO CLOSE", 1, HUD_C_GREY);
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

  char foot[48]; snprintf(foot, sizeof(foot), "CON %d  2G%d 5G%d", n, hud_scan_band_count(2), hud_scan_band_count(5));
  hud_text(8, FOOT_Y, foot, 1, HUD_C_GREEN);
}

// ---- SCAN list cursor (highlighted row) + scroll window ----
static int s_sel = 0, s_scroll = 0;
#define ROW_H 20
static int scan_rows() { return (CONTENT_B - CONTENT_Y) / ROW_H; }

// Band filter: is this channel's band visible under the current filter?
static bool band_ok(uint8_t ch) {
  int b = hud_band_of(ch);
  return s_band == 0 || (s_band == 1 && b == 2) || (s_band == 2 && b == 5);
}
static int vis_count() {
  const Contact* c = hud_scan_list(); int n = hud_scan_count(), k = 0;
  for (int i = 0; i < n; i++) if (band_ok(c[i].ch)) k++;
  return k;
}
static int vis_index(int nth) {      // contact index of the nth visible row, or -1
  const Contact* c = hud_scan_list(); int n = hud_scan_count(), k = 0;
  for (int i = 0; i < n; i++) if (band_ok(c[i].ch)) { if (k == nth) return i; k++; }
  return -1;
}

// Move the highlighted row by d, over the VISIBLE (filtered) set.
static void scan_move(int d) {
  int n = vis_count();
  if (n <= 0) { s_sel = 0; s_scroll = 0; return; }
  s_sel += d;
  if (s_sel < 0) s_sel = 0;
  if (s_sel > n - 1) s_sel = n - 1;
  int rows = scan_rows();
  if (s_sel < s_scroll) s_scroll = s_sel;
  if (s_sel >= s_scroll + rows) s_scroll = s_sel - rows + 1;
}

// ---- SCAN (live): dual-band list, strongest first, band-tagged; cursor + scroll ----
static void page_scan(uint32_t now) {
  (void)now;
  const Contact* c = hud_scan_list();
  int n = vis_count();
  if (s_sel > n - 1) s_sel = n > 0 ? n - 1 : 0;
  int rows = scan_rows();
  if (s_scroll > n - rows) s_scroll = n - rows > 0 ? n - rows : 0;
  if (s_scroll < 0) s_scroll = 0;

  if (hud_scan_count() == 0) hud_text(8, CONTENT_Y + 8, "SCANNING...", 1, HUD_C_CYAN);

  for (int r = 0; r < rows; r++) {
    int vi = s_scroll + r;
    if (vi >= n) break;
    int i = vis_index(vi);
    if (i < 0) break;
    int y = CONTENT_Y + 2 + r * ROW_H;
    bool cur = (vi == s_sel);
    bool tgt = hud_scan_has_target() && !strcmp(c[i].name, hud_scan_target_name());
    uint16_t bandcol = (hud_band_of(c[i].ch) == 5) ? HUD_C_CYAN : HUD_C_GREEN;
    if (cur) hud_fill_rect(0, y - 1, HUD_W, ROW_H - 2, HUD_C_STRIP);           // cursor row
    hud_num(4, y + 1, c[i].rssi < 0 ? -c[i].rssi : c[i].rssi, 1, HUD_C_GREY);  // |dBm|
    hud_text(28, y + 2, hud_band_of(c[i].ch) == 5 ? "5G" : "2G", 1, bandcol);  // band tag
    hud_text(48, y + 2, c[i].name, 1, tgt ? HUD_C_RED : HUD_C_WHITE);          // SSID
    int bar = (int)(rssi_unit(c[i].rssi) * 56);
    hud_fill_rect(HUD_W - 8 - bar, y + 2, bar, 8, bandcol);                    // bar, coloured by band
  }
  // footer: band counts + active filter
  const char* f = s_band == 1 ? "2.4" : s_band == 2 ? "5G" : "ALL";
  char foot[48];
  snprintf(foot, sizeof(foot), "2G%d 5G%d [%s]", hud_scan_band_count(2), hud_scan_band_count(5), f);
  hud_text(8, FOOT_Y, foot, 1, HUD_C_CYAN);
}

// ---- network info view (Marauder/Bruce style): full detail for one AP ----
static void draw_detail() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  if (s_detail_ci < 0 || s_detail_ci >= hud_scan_count()) {
    hud_text(8, CONTENT_Y + 8, "(CONTACT GONE)", 1, HUD_C_GREY);
    hud_text(8, CONTENT_B - 16, "TAP TO CLOSE", 1, HUD_C_GREY);
    return;
  }
  const Contact* c = &hud_scan_list()[s_detail_ci];
  int b = hud_band_of(c->ch);
  int y = CONTENT_Y + 6;
  hud_text(8, y, c->name[0] ? c->name : "(hidden)", 1, HUD_C_WHITE); y += 20;
  char ln[40];
  snprintf(ln, sizeof(ln), "BAND  %s", b == 5 ? "5 GHZ" : "2.4 GHZ");
  hud_text(8, y, ln, 1, b == 5 ? HUD_C_CYAN : HUD_C_GREEN); y += 15;
  snprintf(ln, sizeof(ln), "CHAN  %d", c->ch);              hud_text(8, y, ln, 1, HUD_C_GREY);  y += 15;
  snprintf(ln, sizeof(ln), "RSSI  %d DBM", c->rssi);        hud_text(8, y, ln, 1, HUD_C_GREY);  y += 15;
  snprintf(ln, sizeof(ln), "ENC   %s", hud_enc_name(c->enc));
  hud_text(8, y, ln, 1, c->enc == 0 ? HUD_C_RED : HUD_C_GREEN); y += 15;
  snprintf(ln, sizeof(ln), "BSSID %02X:%02X:%02X:%02X:%02X:%02X",
           c->bssid[0], c->bssid[1], c->bssid[2], c->bssid[3], c->bssid[4], c->bssid[5]);
  hud_text(8, y, ln, 1, HUD_C_GREY);
  hud_text(8, CONTENT_B - 16, "TAP TO CLOSE", 1, HUD_C_GREY);
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

// ---- boot splash: the real H.O.M.E logo, shaking + glitch-distorting in place ----
// Animated like the firmware's boot loader, but instead of bits dropping off the
// screen the logo SHAKES in place with a distortion tear that decays to a steady
// hold, then the HUD starts. (bitmap_home_logo is the 160x160 PUEO_LOGO_BITMAP.)
static int cen(const char* s, int sc) { return HUD_W / 2 - hud_text_w(s, sc) / 2; }

// The official info block under the logo: product, divider, build+version, byline,
// target, and the URL pinned to the bottom. Matches the firmware's boot page.
static void splash_info() {
  int y = 186;
  hud_text(cen(HUD_PRODUCT, 2), y, HUD_PRODUCT, 2, HUD_C_RED);  y += 24;
  hud_fill_rect(14, y, HUD_W - 28, 1, HUD_C_AMBER);            y += 9;
  char v[40]; snprintf(v, sizeof(v), "%s  v.%s", HUD_BUILD, HUD_VERSION);
  hud_text(cen(v, 1), y, v, 1, HUD_C_AMBER);                   y += 15;
  char by[40]; snprintf(by, sizeof(by), "BY %s", HUD_AUTHOR);
  hud_text(cen(by, 1), y, by, 1, HUD_C_GREY);                  y += 14;
  hud_text(cen(HUD_TARGET, 1), y, HUD_TARGET, 1, HUD_C_GREY);
  hud_text(cen(HUD_URL, 1), HUD_H - 16, HUD_URL, 1, HUD_C_GREY);
}

void hud_draw_splash() {
  const int lx = (HUD_W - HOME_LOGO_W) / 2;        // 160 wide -> x = 40
  const int ly = 18;
  const uint32_t DUR = 2400;
  uint32_t t0 = millis();

  // trippy palette the logo strobes through every frame (fast = hallucinate)
  static const uint16_t trip[] = {
    HUD_C_RED, HUD_C_AMBER, HUD_C_GREEN, HUD_C_CYAN,
    hud_rgb(255, 0, 255), hud_rgb(130, 70, 255), hud_rgb(0, 120, 255), HUD_C_WHITE
  };
  const int NT = sizeof(trip) / sizeof(trip[0]);
  uint32_t frame = 0;

  // phase 1: logo shakes + tears in place while the colours strobe fast
  while (millis() - t0 < DUR) {
    uint32_t e = millis() - t0;
    int amp = 7 - (int)(e * 7 / DUR);              // amplitude 7 -> 0 (settles)
    if (amp < 0) amp = 0;
    int dx = amp ? (int)random(-amp, amp + 1) : 0;
    int dy = amp ? (int)random(-amp, amp + 1) : 0;

    hud_clear(HUD_C_BG);
    hud_bitmap1(lx + dx, ly + dy, HOME_LOGO_W, HOME_LOGO_H, bitmap_home_logo, trip[frame % NT], amp);
    hud_text(cen(HUD_PRODUCT, 2), 186, HUD_PRODUCT, 2, trip[(frame + 3) % NT]);
    hud_present_fb(hud_framebuffer(), HUD_W, HUD_H);
    frame++;
    delay(16);                                     // ~60 fps -> colours cycle fast
  }

  // phase 2: steady official boot card (logo + full info block)
  hud_clear(HUD_C_BG);
  hud_bitmap1(lx, ly, HOME_LOGO_W, HOME_LOGO_H, bitmap_home_logo, HUD_C_WHITE, 0);
  splash_info();
  hud_present_fb(hud_framebuffer(), HUD_W, HUD_H);
  delay(1300);
}

void hud_page_draw(int mode, uint32_t now) {
  if (s_detail) {
    draw_detail();     // network info view (modal over the content band)
  } else if (s_settings) {
    draw_settings();   // modal panel over the content band
  } else {
    switch (mode) {
      case M_SCAN:   page_scan(now);   break;
      case M_RADAR:  page_radar(now);  break;
      case M_MAP:    page_map(now);    break;
      case M_COMMS:  page_comms(now);  break;
      case M_ENGAGE: page_engage(now); break;
      default: break;
    }
  }
  draw_chrome(mode);   // chrome last so strips/gear/tabs sit on top
}

// Which scan zone is y in? -1 = scroll up, +1 = scroll down, 0 = centre/enter.
static int scan_zone(int y) {
  int band = CONTENT_B - CONTENT_Y;
  if (y < CONTENT_Y + band / 3) return -1;
  if (y > CONTENT_B - band / 3) return +1;
  return 0;
}

void hud_on_press(int x, int y) {
  // settings gear (top-right of the status strip) toggles the SETTINGS panel
  if (y < STAT_H && x > HUD_W - 24) { s_settings = !s_settings; s_detail = false; return; }

  if (s_detail) { s_detail = false; return; } // info view: any tap closes it

  if (s_settings) {                           // panel open = modal
    if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
        y >= SET_BTN_Y && y <= SET_BTN_Y + SET_BTN_H) {
      hud_request_recal(); s_settings = false; // RECALIBRATE TOUCH
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_BND_Y && y <= SET_BND_Y + SET_BTN_H) {
      s_band = (s_band + 1) % 3;              // BAND: ALL -> 2.4 -> 5 (stay in panel)
      s_sel = 0; s_scroll = 0;
    } else {
      s_settings = false;                     // tap elsewhere closes
    }
    return;
  }

  if (y >= HUD_H - TAB_H) {                   // bottom tab strip = switch page
    int m = x / (HUD_W / M_COUNT);
    if (m >= 0 && m < M_COUNT) { hud_mode_set(m); s_manual = true; }
    return;
  }
  if (hud_mode_get() == M_SCAN) {
    int z = scan_zone(y);                      // upper = scroll up, lower = scroll down
    if (z == -1) scan_move(-1);
    else if (z == +1) scan_move(+1);
    else {                                     // centre = ENTER: open the info view
      int i = vis_index(s_sel);
      if (i >= 0) { s_detail_ci = i; s_detail = true; }
    }
  }
}

void hud_on_repeat(int x, int y) {
  (void)x;
  if (s_settings || s_detail) return;         // no auto-repeat while a panel is open
  if (y >= HUD_H - TAB_H) return;             // tabs/enter don't auto-repeat
  if (hud_mode_get() == M_SCAN) {
    int z = scan_zone(y);
    if (z == -1) scan_move(-1);
    else if (z == +1) scan_move(+1);
  }
}
