#include "hud_pages.h"
#include "hud_core.h"
#include "hud_scan.h"
#include "hud_gps.h"
#include "hud_comms.h"
#include "hud_engage.h"
#include "hud_oui.h"
#include "home_logo.h"
#include "branding.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// NIGHT / STEALTH mode (dim backlight + red tint), implemented in hud.ino.
bool hud_stealth();
void hud_set_stealth(bool on);

// Map a signal RSSI (dBm, ~-30 near .. ~-95 far) to 0..1 (1 = strongest).
static float rssi_unit(int8_t rssi) {
  float u = (rssi + 95) / 60.0f;          // -95 -> 0, -35 -> 1
  return u < 0 ? 0 : u > 1 ? 1 : u;
}
// metres + bearing(deg, 0=N) from (lat1,lon1) to (lat2,lon2). Shared by RADAR + ENGAGE.
static double geo_dist_brg(double lat1, double lon1, double lat2, double lon2, float* brg) {
  double dN = (lat2 - lat1) * 111320.0;
  double dE = (lon2 - lon1) * 111320.0 * cos(lat1 * 0.01745);
  float b = atan2f((float)dE, (float)dN) * 57.2958f; if (b < 0) b += 360;
  *brg = b;
  return sqrt(dN * dN + dE * dE);
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
#define FOOT_H    13                    // page footer bar, at the BOTTOM of the content band
                                        // (chrome fills the TOP strip last, so a top-strip footer gets painted over)

// settings gear (top-right of the top strip) + the SETTINGS panel
static bool s_settings = false;
static int  s_band = 0;              // band filter: 0 = ALL, 1 = 2.4 GHz, 2 = 5 GHz
static bool s_detail = false;        // network info view open
static int  s_detail_ci = -1;        // which contact index it shows
// COMMS composer
static bool s_kb = false;            // keyboard expanded (vs the collapsed quick-strip)
static bool s_setnet = false;        // keyboard is entering a NET passphrase (not a message)
static char s_compose[CHAT_TEXT_MAX + 1] = {0};
static int  s_qmpage = 0;            // quick-message page in the collapsed strip (4/page)
#define GEAR_X    (HUD_W - 13)
#define GEAR_Y    (STAT_H / 2)
#define SET_BTN_X 20
#define SET_BTN_Y (CONTENT_Y + 44)
#define SET_BTN_W (HUD_W - 40)
#define SET_BTN_H 30
#define SET_BND_Y (SET_BTN_Y + SET_BTN_H + 14)   // BAND filter row, below RECALIBRATE
#define SET_STL_Y (SET_BND_Y + SET_BTN_H + 14)    // NIGHT/STEALTH row, below BAND

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

// trippy colour palette shared by the boot splash and the flashing HOME badge
static const uint16_t s_trip[] = {
  HUD_C_RED, HUD_C_AMBER, HUD_C_GREEN, HUD_C_CYAN,
  hud_rgb(255, 0, 255), hud_rgb(130, 70, 255), hud_rgb(0, 120, 255), HUD_C_WHITE
};
#define S_NT ((int)(sizeof(s_trip) / sizeof(s_trip[0])))
static inline uint16_t trip_now() { return s_trip[(millis() / 55) % S_NT]; }  // fast strobe

// ---- top status strip + BOTTOM tab strip (shared chrome) ----
static void draw_chrome(int mode) {
  hud_fill_rect(0, 0, HUD_W, STAT_H, HUD_C_STRIP);           // top status strip
  hud_text(3, 6, "HOME", 2, trip_now());                     // flashing HOME badge, top-left
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

  // ---- GLOBAL THREAT ALERT: strobes across the top strip on ANY page when the
  // ENGAGE sniffer holds a live drone or a recent deauth. Detection only runs on
  // RADAR/ENGAGE, but a drone contact stays live ~12 s and an attack ~8 s, so the
  // warning follows you onto SCAN/MAP/COMMS for that window after you leave the scope.
  bool threat_drone = hud_engage_drone_count() > 0;
  uint32_t threat_lm = hud_engage_last_ms();
  bool threat_atk = threat_lm && (millis() - threat_lm < 8000);
  if ((threat_drone || threat_atk) && ((millis() / 250) & 1)) {   // ~2 Hz strobe
    hud_fill_rect(0, 0, HUD_W, STAT_H, HUD_C_RED);
    const char* t = threat_drone ? "! DRONE DETECTED" : "! WIFI ATTACK";
    hud_text(HUD_W / 2 - hud_text_w(t, 1) / 2, (STAT_H - 7) / 2, t, 1, HUD_C_WHITE);
  }
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
  // NIGHT / STEALTH toggle (dim backlight + red night-vision tint)
  bool st = hud_stealth();
  uint16_t sc = st ? HUD_C_RED : HUD_C_GREY;
  hud_rect(SET_BTN_X, SET_STL_Y, SET_BTN_W, SET_BTN_H, sc);
  char sl[24]; snprintf(sl, sizeof(sl), "STEALTH:  %s", st ? "ON" : "OFF");
  hud_text(HUD_W / 2 - hud_text_w(sl, 1) / 2, SET_STL_Y + (SET_BTN_H - 7) / 2, sl, 1, sc);
  hud_text(HUD_W / 2 - hud_text_w("TAP GEAR TO CLOSE", 1) / 2, CONTENT_B - 16, "TAP GEAR TO CLOSE", 1, HUD_C_GREY);
}

// ---- RADAR: live spatial threat scope. Plots Remote-ID drones at their REAL bearing
// + range (from your GPS + the drone's own broadcast position); flashes on a WiFi
// attack. (RF-emitter bearings need the DF antenna -- added to this scope when it lands.)
static void page_radar(uint32_t now) {
  static float sweep = 0.0f;
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2, R = (CONTENT_B - CONTENT_Y) / 2 - 4;
  const GpsFix& g = hud_gps();
  int dn = hud_engage_drone_count();

  // auto range: farthest drone (min 300 m), rounded up to 100 m
  double maxR = 300;
  DroneInfo d;
  if (g.valid)
    for (int i = 0; i < dn; i++) {
      if (hud_engage_drone(i, &d) && d.loc) { float b; double dist = geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b); if (dist > maxR) maxR = dist; }
    }
  maxR = ((int)(maxR / 100) + 1) * 100.0;

  // rings + crosshair + sweep + N
  hud_ring(cx, cy, R, HUD_C_DGREEN); hud_ring(cx, cy, R * 2 / 3, HUD_C_DGREEN); hud_ring(cx, cy, R / 3, HUD_C_DGREEN);
  hud_line(cx - R, cy, cx + R, cy, HUD_C_DGREEN); hud_line(cx, cy - R, cx, cy + R, HUD_C_DGREEN);
  sweep += 0.07f; if (sweep > 6.2832f) sweep -= 6.2832f;
  hud_line(cx, cy, cx + (int)(cosf(sweep) * R), cy + (int)(sinf(sweep) * R), HUD_C_GREEN);
  hud_text(cx - 2, CONTENT_Y + 2, "N", 1, HUD_C_WHITE);

  // WiFi attack -> flashing red perimeter
  if (hud_engage_last_ms() && (now - hud_engage_last_ms() < 5000) && ((now / 250) & 1))
    hud_ring(cx, cy, R - 1, HUD_C_RED);

  // drones at true bearing + range
  if (g.valid)
    for (int i = 0; i < dn; i++) {
      if (!hud_engage_drone(i, &d) || !d.loc) continue;
      float b; double dist = geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b);
      double rr = (dist / maxR) * R; if (rr > R) rr = R;
      int bx = cx + (int)(sinf(b * 0.01745f) * rr), by = cy - (int)(cosf(b * 0.01745f) * rr);
      hud_disc(bx, by, 3, HUD_C_RED); hud_ring(bx, by, 5, HUD_C_RED);
    }
  hud_disc(cx, cy, 2, HUD_C_WHITE);                 // you

  // readout panel (in content, not the clipped strip)
  hud_fill_rect(0, CONTENT_B - 13, HUD_W, 13, HUD_C_STRIP);
  char ln[48];
  if (!g.valid) snprintf(ln, sizeof(ln), "DRONES %d   NO GPS", dn);
  else          snprintf(ln, sizeof(ln), "DRONES %d   RANGE %dM", dn, (int)maxR);
  hud_text(6, CONTENT_B - 10, ln, 1, dn ? HUD_C_RED : HUD_C_GREEN);
}

// ---- SCAN list cursor (highlighted row) + scroll window ----
static int s_sel = 0, s_scroll = 0;
#define ROW_H 20
static int scan_rows() { return (CONTENT_B - CONTENT_Y - FOOT_H) / ROW_H; }  // reserve the footer row

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
  hud_fill_rect(0, CONTENT_B - FOOT_H, HUD_W, FOOT_H, HUD_C_STRIP);
  hud_text(6, CONTENT_B - FOOT_H + 3, foot, 1, HUD_C_CYAN);
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
  hud_text(8, y, ln, 1, HUD_C_GREY); y += 15;
  // vendor hint from the OUI (IEEE registry); drone makers stand out in red
  const char* ven = hud_oui_vendor(c->bssid);
  bool drone = ven && (!strcmp(ven, "DJI") || !strcmp(ven, "PARROT") ||
                       !strcmp(ven, "SKYDIO") || !strcmp(ven, "AUTEL"));
  snprintf(ln, sizeof(ln), "VENDOR %s", ven ? ven : "UNKNOWN");
  hud_text(8, y, ln, 1, drone ? HUD_C_RED : (ven ? HUD_C_AMBER : HUD_C_GREY));
  hud_text(8, CONTENT_B - 16, "TAP TO CLOSE", 1, HUD_C_GREY);
}

// ---- MAP (grid + you + contacts) ----
// ---- MAP: GPS moving-map base (grid + you-marker + breadcrumb trail + heading/speed).
// Topo tiles render under the grid once an SD card + tiles are present. ----
#define TRAIL_MAX 40
#define MPP 3.0                       // metres per pixel on the MAP
static double s_trLat[TRAIL_MAX], s_trLon[TRAIL_MAX];
static int    s_trCount = 0;
static uint32_t s_trLast = 0;
// "moving" from real displacement over a window (rejects stationary GPS speed jitter)
static double   s_refLat = 0, s_refLon = 0;
static uint32_t s_refT = 0;
static bool     s_moving = false;
static float    s_spdMph = 0, s_hdg = 0;
// MAP waypoint: one mark you drop at your position, then navigate back to (range+bearing).
static bool     s_wpSet = false;
static double   s_wpLat = 0, s_wpLon = 0;
#define MOVE_M   4.0            // metres over the window to count as moving (~3 mph)
#define MOVE_MS  3000
static const char* cardinal(float deg) {
  static const char* C[8] = { "N", "NE", "E", "SE", "S", "SW", "W", "NW" };
  return C[((int)((deg + 22.5f) / 45.0f)) & 7];
}

static void page_map(uint32_t now) {
  const GpsFix& g = hud_gps();
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2;

  // grid that scrolls with position (so it reads as a moving map once GPS is live)
  int ox = 0, oy = 0;
  if (g.valid) {
    ox = (int)(fmod(g.lon * 100000.0, 32.0));
    oy = (int)(fmod(g.lat * 100000.0, 32.0));
  }
  for (int gx = -32 + ((ox % 32) + 32) % 32; gx < HUD_W; gx += 32)
    hud_fill_rect(gx, CONTENT_Y, 1, CONTENT_B - CONTENT_Y, HUD_C_DGREEN);
  for (int gy = CONTENT_Y - 32 + ((oy % 32) + 32) % 32; gy < CONTENT_B; gy += 32)
    if (gy >= CONTENT_Y) hud_fill_rect(0, gy, HUD_W, 1, HUD_C_DGREEN);
  hud_text(cx + 8, CONTENT_Y + 4, "N", 1, HUD_C_WHITE);   // north hint

  // breadcrumb: record a point ~once/sec when it has moved > ~2 m
  if (g.valid && (now - s_trLast > 1000)) {
    bool far = true;
    if (s_trCount > 0) {
      double dy = (g.lat - s_trLat[s_trCount - 1]) * 111320.0;
      double dx = (g.lon - s_trLon[s_trCount - 1]) * 111320.0 * cos(g.lat * 0.01745);
      far = (dx * dx + dy * dy) > 4.0;
    }
    if (far) {
      if (s_trCount >= TRAIL_MAX) {
        memmove(s_trLat, s_trLat + 1, sizeof(double) * (TRAIL_MAX - 1));
        memmove(s_trLon, s_trLon + 1, sizeof(double) * (TRAIL_MAX - 1));
        s_trCount = TRAIL_MAX - 1;
      }
      s_trLat[s_trCount] = g.lat; s_trLon[s_trCount] = g.lon; s_trCount++;
      s_trLast = now;
    }
  }
  // draw the trail relative to the current position (you = centre, N up)
  if (g.valid) {
    double cosl = cos(g.lat * 0.01745);
    for (int i = 0; i < s_trCount; i++) {
      int bx = cx + (int)(((s_trLon[i] - g.lon) * 111320.0 * cosl) / MPP);
      int by = cy - (int)(((s_trLat[i] - g.lat) * 111320.0) / MPP);
      if (bx >= 0 && bx < HUD_W && by >= CONTENT_Y && by < CONTENT_B)
        hud_disc(bx, by, 1, HUD_C_AMBER);
    }
  }

  // moving? decide from displacement over MOVE_MS, and derive speed+heading from it
  if (g.valid) {
    if (s_refT == 0) { s_refLat = g.lat; s_refLon = g.lon; s_refT = now; }
    uint32_t dt = now - s_refT;
    if (dt >= MOVE_MS) {
      double dy = (g.lat - s_refLat) * 111320.0;
      double dx = (g.lon - s_refLon) * 111320.0 * cos(g.lat * 0.01745);
      double dist = sqrt(dx * dx + dy * dy);
      if (dist > MOVE_M) {
        s_moving = true;
        s_spdMph = (float)(dist / (dt / 1000.0) * 2.23694);
        s_hdg = atan2f((float)dx, (float)dy) * 57.2958f; if (s_hdg < 0) s_hdg += 360;
      } else {
        s_moving = false;
      }
      s_refLat = g.lat; s_refLon = g.lon; s_refT = now;   // new window
    }
  } else { s_moving = false; s_refT = 0; }

  // waypoint marker (cyan diamond) at its position relative to you, if on screen
  if (g.valid && s_wpSet) {
    double cosl = cos(g.lat * 0.01745);
    int wx = cx + (int)(((s_wpLon - g.lon) * 111320.0 * cosl) / MPP);
    int wy = cy - (int)(((s_wpLat - g.lat) * 111320.0) / MPP);
    if (wx >= 2 && wx < HUD_W - 2 && wy >= CONTENT_Y + 2 && wy < CONTENT_B - 2) {
      hud_line(wx, wy - 4, wx + 4, wy, HUD_C_CYAN); hud_line(wx + 4, wy, wx, wy + 4, HUD_C_CYAN);
      hud_line(wx, wy + 4, wx - 4, wy, HUD_C_CYAN); hud_line(wx - 4, wy, wx, wy - 4, HUD_C_CYAN);
    }
  }

  // you-marker: arrow along the travel heading when moving, else up (N)
  float a = (s_moving ? s_hdg : 0.0f) * 0.01745329f;
  int tx = cx + (int)(sinf(a) * 9), ty = cy - (int)(cosf(a) * 9);
  int lx2 = cx + (int)(sinf(a + 2.6f) * 7), ly2 = cy - (int)(cosf(a + 2.6f) * 7);
  int rx2 = cx + (int)(sinf(a - 2.6f) * 7), ry2 = cy - (int)(cosf(a - 2.6f) * 7);
  uint16_t mc = g.valid ? HUD_C_GREEN : HUD_C_GREY;
  hud_line(tx, ty, lx2, ly2, mc); hud_line(tx, ty, rx2, ry2, mc); hud_line(lx2, ly2, rx2, ry2, mc);

  // readout panel (two lines): position, then heading/speed (moving) or status
  hud_fill_rect(0, CONTENT_B - 26, HUD_W, 26, HUD_C_STRIP);
  char ln[48];
  if (g.valid) {
    snprintf(ln, sizeof(ln), "%.5f %.5f", g.lat, g.lon);
    hud_text(6, CONTENT_B - 22, ln, 1, HUD_C_GREEN);
    if (s_wpSet) {                               // navigating to a waypoint
      float wb; double wd = geo_dist_brg(g.lat, g.lon, s_wpLat, s_wpLon, &wb);
      snprintf(ln, sizeof(ln), "WP %dM  %03d %s  TAP:CLEAR", (int)wd, (int)wb, cardinal(wb));
      hud_text(6, CONTENT_B - 10, ln, 1, HUD_C_CYAN);
    } else if (s_moving) {
      snprintf(ln, sizeof(ln), "HDG %03d %s  %.1f MPH  SAT %d",
               (int)s_hdg, cardinal(s_hdg), s_spdMph, g.sats);
      hud_text(6, CONTENT_B - 10, ln, 1, HUD_C_GREY);
    } else {
      snprintf(ln, sizeof(ln), "STOPPED  SAT %d  TAP:DROP WP", g.sats);
      hud_text(6, CONTENT_B - 10, ln, 1, HUD_C_GREY);
    }
  } else {
    hud_text(6, CONTENT_B - 22, "ACQUIRING GPS...", 1, HUD_C_AMBER);
    snprintf(ln, sizeof(ln), "SAT %d   NO FIX", g.sats);
    hud_text(6, CONTENT_B - 10, ln, 1, HUD_C_GREY);
  }
}

// ---- COMMS (chat placeholder) ----
// ---- COMMS composer: a collapsible on-screen keyboard; when collapsed, a scrollable
// strip of canned quick-messages. Free text (and later net passphrases) via the keyboard.
#define KB_KEYH 19
static const char* const KB_ROWS[] = { "1234567890", "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM" };
#define KB_NROWS 4
// collapsed quick-message area: a 2x2 grid (4 per page) + a nav row; 2 pages for 8 msgs
#define CHIP_ROWS 2
#define CHIP_COLS 2
#define CHIP_PP   (CHIP_ROWS * CHIP_COLS)
#define CHIP_H    24
#define NAV_H     20
static int kb_top()    { return CONTENT_B - (KB_NROWS + 1) * KB_KEYH; }  // +1 = special row
static int nav_y()     { return CONTENT_B - NAV_H; }
static int strip_top() { return nav_y() - CHIP_ROWS * CHIP_H; }
static int qm_pages()  { int n = hud_quickmsg_count(); return (n + CHIP_PP - 1) / CHIP_PP; }
// collapsed-strip zones
#define STRIP_L  (-3)
#define STRIP_R  (-2)
#define STRIP_KB (-4)
#define STRIP_NONE (-99)

static char kb_hit(int x, int y) {
  int top = kb_top();
  if (y < top) return 0;
  int row = (y - top) / KB_KEYH;
  if (row < KB_NROWS) {
    const char* r = KB_ROWS[row]; int L = (int)strlen(r);
    int x0 = (HUD_W - L * 24) / 2;
    if (x < x0) return 0;
    int i = (x - x0) / 24;
    if (i < 0 || i >= L) return 0;
    return r[i];
  }
  int i = x / 60;                        // special row: SPC / DEL / SEND / collapse
  if (i == 0) return ' ';
  if (i == 1) return '\b';
  if (i == 2) return '\n';
  return 0x1B;
}
static int strip_hit(int x, int y) {
  if (y < strip_top()) return STRIP_NONE;
  if (y >= nav_y()) {                       // nav row
    if (x < 56) return STRIP_L;             // prev page
    if (x < 112) return STRIP_R;            // next page
    if (x >= 184) return STRIP_KB;          // open keyboard
    return STRIP_NONE;
  }
  int col = (x < 118) ? 0 : (x >= 122 ? 1 : -1);
  if (col < 0) return STRIP_NONE;
  int row = (y - strip_top()) / CHIP_H;
  int idx = s_qmpage * CHIP_PP + row * CHIP_COLS + col;
  return (idx < hud_quickmsg_count()) ? idx : STRIP_NONE;
}

static void draw_kb() {
  int top = kb_top();
  char cl[64]; snprintf(cl, sizeof(cl), "%s%s", s_setnet ? "NET>" : ">", s_compose);  // compose line
  hud_text(6, top - 13, cl, 1, HUD_C_WHITE);
  for (int row = 0; row < KB_NROWS; row++) {
    const char* r = KB_ROWS[row]; int L = (int)strlen(r); int x0 = (HUD_W - L * 24) / 2;
    int y = top + row * KB_KEYH;
    for (int i = 0; i < L; i++) {
      int kx = x0 + i * 24; hud_rect(kx, y, 23, KB_KEYH - 1, HUD_C_GREY);
      char s[2] = { r[i], 0 }; hud_text(kx + 8, y + (KB_KEYH - 7) / 2, s, 1, HUD_C_WHITE);
    }
  }
  int y = top + KB_NROWS * KB_KEYH;
  const char* lab[4] = { "SPC", "DEL", "SEND", "v" };
  uint16_t col[4] = { HUD_C_GREY, HUD_C_AMBER, HUD_C_GREEN, HUD_C_CYAN };
  for (int i = 0; i < 4; i++) {
    int bx = i * 60; hud_rect(bx, y, 59, KB_KEYH - 1, col[i]);
    hud_text(bx + (60 - hud_text_w(lab[i], 1)) / 2, y + (KB_KEYH - 7) / 2, lab[i], 1, col[i]);
  }
}
static void draw_strip() {
  int top = strip_top();
  for (int i = 0; i < CHIP_PP; i++) {
    int idx = s_qmpage * CHIP_PP + i;
    if (idx >= hud_quickmsg_count()) break;
    int col = i % CHIP_COLS, row = i / CHIP_COLS;
    int bx = 4 + col * 118, by = top + row * CHIP_H;
    hud_rect(bx, by, 114, CHIP_H - 2, HUD_C_AMBER);
    const char* t = HUD_QUICKMSG[idx];
    hud_text(bx + (114 - hud_text_w(t, 1)) / 2, by + (CHIP_H - 2 - 7) / 2, t, 1, HUD_C_AMBER);
  }
  int ny = nav_y();
  hud_rect(0,  ny, 54, NAV_H, HUD_C_GREY);  hud_text(24, ny + (NAV_H - 7) / 2, "<", 1, HUD_C_WHITE);
  hud_rect(58, ny, 54, NAV_H, HUD_C_GREY);  hud_text(82, ny + (NAV_H - 7) / 2, ">", 1, HUD_C_WHITE);
  char pg[24]; snprintf(pg, sizeof(pg), "P%d/%d", s_qmpage + 1, qm_pages());
  hud_text(120, ny + (NAV_H - 7) / 2, pg, 1, HUD_C_GREY);
  hud_rect(184, ny, 55, NAV_H, HUD_C_CYAN);
  hud_text(184 + (55 - hud_text_w("KEYBD", 1)) / 2, ny + (NAV_H - 7) / 2, "KEYBD", 1, HUD_C_CYAN);
}

#define NET_Y (CONTENT_Y + 3)        // tappable "NET: xxx" header
static void page_comms(uint32_t now) {
  (void)now;
  // NET header (tap to set a passphrase); OPEN = green, a keyed group = cyan
  char nh[32]; snprintf(nh, sizeof(nh), "NET: %s", hud_comms_net());
  hud_text(6, NET_Y, nh, 1, hud_comms_net_open() ? HUD_C_GREEN : HUD_C_CYAN);
  hud_text(HUD_W - hud_text_w("[SET]", 1) - 6, NET_Y, "[SET]", 1, HUD_C_GREY);

  int bottom = s_kb ? kb_top() - 14 : strip_top();
  const ChatMsg* L = hud_comms_log();
  int n = hud_comms_count();
  int logTop = CONTENT_Y + 16;
  int rowsFit = (bottom - logTop) / 14; if (rowsFit < 1) rowsFit = 1;
  int start = n > rowsFit ? n - rowsFit : 0;
  int y = logTop;
  if (n == 0) hud_text(8, y, "NO TRAFFIC", 1, HUD_C_GREY);
  for (int i = start; i < n; i++) {
    char line[64];
    snprintf(line, sizeof(line), "%s%s: %s", L[i].open ? "*" : "", L[i].me ? "ME" : L[i].from, L[i].text);
    uint16_t col = L[i].me ? HUD_C_GREEN : (L[i].open ? HUD_C_CYAN : HUD_C_AMBER);
    hud_text(6, y, line, 1, col);
    y += 14;
  }
  if (s_kb) draw_kb(); else draw_strip();
}

// ---- ENGAGE: passive threat detector -- WiFi deauth attacks + Remote-ID drones ----
static void page_engage(uint32_t now) {
  int cx = HUD_W / 2;
  bool recent = hud_engage_last_ms() && (now - hud_engage_last_ms() < 5000);
  int dn = hud_engage_drone_count();
  const GpsFix& g = hud_gps();
  char ln[48];

  hud_text(cx - hud_text_w("THREAT DETECT", 2) / 2, CONTENT_Y + 6, "THREAT DETECT", 2, HUD_C_RED);

  int y = CONTENT_Y + 30;
  if (recent) {                                   // WiFi deauth/jam attack
    if ((now / 250) & 1) { hud_fill_rect(6, y - 2, HUD_W - 12, 13, HUD_C_RED); hud_text(10, y, "WIFI ATTACK: DEAUTH", 1, HUD_C_BG); }
    else hud_text(10, y, "WIFI ATTACK: DEAUTH", 1, HUD_C_RED);
  } else hud_text(10, y, "WIFI: CLEAR", 1, HUD_C_GREEN);
  y += 18;

  snprintf(ln, sizeof(ln), "DRONES: %d", dn);     // Remote-ID drones
  hud_text(10, y, ln, 1, dn ? HUD_C_RED : HUD_C_GREY); y += 14;
  for (int i = 0; i < dn && i < 3; i++) {
    DroneInfo d; if (!hud_engage_drone(i, &d)) break;
    hud_text(14, y, d.id[0] ? d.id : "(NO ID)", 1, HUD_C_AMBER); y += 12;
    if (d.loc && g.valid) {
      float b; int dist = (int)geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b);
      snprintf(ln, sizeof(ln), " DRN %dM @%03d%s", dist, (int)b, cardinal(b));
      hud_text(14, y, ln, 1, HUD_C_RED); y += 12;
    } else if (d.loc) {
      snprintf(ln, sizeof(ln), " %.5f %.5f", d.lat, d.lon); hud_text(14, y, ln, 1, HUD_C_GREY); y += 12;
    }
    if (d.op && g.valid) {
      float b; int dist = (int)geo_dist_brg(g.lat, g.lon, d.oplat, d.oplon, &b);
      snprintf(ln, sizeof(ln), " OP  %dM @%03d%s", dist, (int)b, cardinal(b));
      hud_text(14, y, ln, 1, HUD_C_AMBER); y += 12;
    }
  }

  hud_fill_rect(0, CONTENT_B - 13, HUD_W, 13, HUD_C_STRIP);   // sniffer readout
  snprintf(ln, sizeof(ln), "DEAUTH %lu  SNIFF %lu  CH %d",
           (unsigned long)hud_engage_deauth(), (unsigned long)hud_engage_frames(), hud_engage_channel());
  hud_text(6, CONTENT_B - 10, ln, 1, HUD_C_GREY);
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

  // the logo strobes through the shared trippy palette every frame (fast = hallucinate)
  const int NT = S_NT;
  uint32_t frame = 0;

  // phase 1: logo shakes + tears in place while the colours strobe fast
  while (millis() - t0 < DUR) {
    uint32_t e = millis() - t0;
    int amp = 7 - (int)(e * 7 / DUR);              // amplitude 7 -> 0 (settles)
    if (amp < 0) amp = 0;
    int dx = amp ? (int)random(-amp, amp + 1) : 0;
    int dy = amp ? (int)random(-amp, amp + 1) : 0;

    hud_clear(HUD_C_BG);
    hud_bitmap1(lx + dx, ly + dy, HOME_LOGO_W, HOME_LOGO_H, bitmap_home_logo, s_trip[frame % NT], amp);
    hud_text(cen(HUD_PRODUCT, 2), 186, HUD_PRODUCT, 2, s_trip[(frame + 3) % NT]);
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
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_STL_Y && y <= SET_STL_Y + SET_BTN_H) {
      hud_set_stealth(!hud_stealth());        // NIGHT/STEALTH toggle (stay in panel)
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
  } else if (hud_mode_get() == M_COMMS) {
    if (!s_kb && y < CONTENT_Y + 14) {          // tap the NET header -> enter a passphrase
      s_kb = true; s_setnet = true; s_compose[0] = 0; return;
    }
    if (s_kb) {                                 // keyboard open: keys compose/send
      char k = kb_hit(x, y);
      if (k == 0x1B) { s_kb = false; s_setnet = false; }          // collapse
      else if (k == '\n') {
        if (s_setnet) { hud_comms_set_net(s_compose); s_setnet = false; s_compose[0] = 0; s_kb = false; }
        else if (s_compose[0]) { hud_comms_send(s_compose); s_compose[0] = 0; }
      }
      else if (k == '\b') { int l = (int)strlen(s_compose); if (l > 0) s_compose[l - 1] = 0; }
      else if (k >= ' ')  { int l = (int)strlen(s_compose); if (l < CHAT_TEXT_MAX) { s_compose[l] = k; s_compose[l + 1] = 0; } }
    } else {                                    // collapsed strip: arrows / chips / keyboard
      int z = strip_hit(x, y);
      if (z == STRIP_KB) s_kb = true;
      else if (z == STRIP_L) { if (s_qmpage > 0) s_qmpage--; }
      else if (z == STRIP_R) { if (s_qmpage < qm_pages() - 1) s_qmpage++; }
      else if (z >= 0) hud_comms_send(HUD_QUICKMSG[z]);
    }
  } else if (hud_mode_get() == M_MAP) {
    // tap toggles the waypoint: drop one at your position, or clear the one you have
    if (s_wpSet) s_wpSet = false;
    else { const GpsFix& g = hud_gps(); if (g.valid) { s_wpLat = g.lat; s_wpLon = g.lon; s_wpSet = true; } }
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
