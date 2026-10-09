#include "hud_pages.h"
#include "hud_core.h"
#include "hud_scan.h"
#include "hud_gps.h"
#include "hud_comms.h"
#include "hud_engage.h"
#include "hud_oui.h"
#include "hud_sd.h"
#include "home_logo.h"
#include "branding.h"
#include <Arduino.h>
#include <math.h>
#include <stdio.h>
#include <string.h>

// NIGHT / STEALTH mode (dim backlight + red tint) + brightness, implemented in hud.ino.
bool hud_stealth();
void hud_set_stealth(bool on);
void hud_set_brightness(uint8_t duty);
uint8_t hud_brightness();

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
static int  s_band = 1;              // which band we scan+show: 1 = 2.4 GHz, 2 = 5 GHz (no auto-switch)
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
#define SET_BTN_W (HUD_W - 40)
#define SET_BTN_H 26
#define SET_BTN_Y (CONTENT_Y + 24)                // first row (denser; fits 6 rows)
#define SET_ROW   (SET_BTN_H + 8)                 // 34 px pitch
#define SET_BND_Y (SET_BTN_Y + SET_ROW)           // BAND filter
#define SET_STL_Y (SET_BND_Y + SET_ROW)           // NIGHT/STEALTH
#define SET_BRT_Y (SET_STL_Y + SET_ROW)           // BRIGHTNESS
#define SET_FIL_Y (SET_BRT_Y + SET_ROW)           // SD FILES
#define SET_INF_Y (SET_FIL_Y + SET_ROW)           // DEVICE INFO

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
// one settings row: outlined box + centred label, all in colour `c`
static void set_row(int y, const char* label, uint16_t c) {
  hud_rect(SET_BTN_X, y, SET_BTN_W, SET_BTN_H, c);
  hud_text(HUD_W / 2 - hud_text_w(label, 1) / 2, y + (SET_BTN_H - 7) / 2, label, 1, c);
}

static void draw_settings() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  hud_text(HUD_W / 2 - hud_text_w("SETTINGS", 2) / 2, CONTENT_Y + 4, "SETTINGS", 2, HUD_C_WHITE);
  set_row(SET_BTN_Y, "RECALIBRATE TOUCH", HUD_C_CYAN);
  char bl[24]; snprintf(bl, sizeof(bl), "BAND:  %s", s_band == 1 ? "2.4 GHZ" : s_band == 2 ? "5 GHZ" : "ALL");
  set_row(SET_BND_Y, bl, HUD_C_AMBER);
  bool st = hud_stealth();
  char sl[24]; snprintf(sl, sizeof(sl), "STEALTH:  %s", st ? "ON" : "OFF");
  set_row(SET_STL_Y, sl, st ? HUD_C_RED : HUD_C_GREY);
  int bpct = (hud_brightness() * 100 + 127) / 255;
  char br[24]; snprintf(br, sizeof(br), "BRIGHTNESS:  %d%%", bpct);
  set_row(SET_BRT_Y, br, HUD_C_GREEN);
  set_row(SET_FIL_Y, hud_sd_ok() ? "SD FILES" : "SD: NO CARD / FORMAT", hud_sd_ok() ? HUD_C_CYAN : HUD_C_AMBER);
  set_row(SET_INF_Y, "DEVICE INFO + GPS", HUD_C_CYAN);
  hud_text(HUD_W / 2 - hud_text_w("TAP GEAR TO CLOSE", 1) / 2, CONTENT_B - 12, "TAP GEAR TO CLOSE", 1, HUD_C_GREY);
}

// ---- RADAR: live spatial threat scope. Plots Remote-ID drones at their REAL bearing
// + range (from your GPS + the drone's own broadcast position); flashes on a WiFi
// attack. (RF-emitter bearings need the DF antenna -- added to this scope when it lands.)
static void page_radar(uint32_t now) {
  static float sweep = 0.0f;
  int cx = HUD_W / 2, cy = (CONTENT_Y + CONTENT_B) / 2, R = (CONTENT_B - CONTENT_Y) / 2 - 4;
  const GpsFix& g = hud_gps();
  int dn = hud_engage_drone_count();
  int fn = g.valid ? hud_comms_friend_count() : 0;   // teammates (blue force)
  FriendInfo fi;

  // auto range: farthest drone / pilot / teammate (min 300 m), rounded up to 100 m
  double maxR = 300;
  DroneInfo d;
  if (g.valid) {
    for (int i = 0; i < dn; i++) {
      if (!hud_engage_drone(i, &d)) continue;
      float b;
      if (d.loc) { double dd = geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b); if (dd > maxR) maxR = dd; }
      if (d.op)  { double od = geo_dist_brg(g.lat, g.lon, d.oplat, d.oplon, &b); if (od > maxR) maxR = od; }
    }
    for (int i = 0; i < fn; i++)
      if (hud_comms_friend(i, &fi)) { float b; double fd = geo_dist_brg(g.lat, g.lon, fi.lat, fi.lon, &b); if (fd > maxR) maxR = fd; }
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

  // drones (red disc) + their pilot/operator (amber cross) at true bearing + range
  if (g.valid)
    for (int i = 0; i < dn; i++) {
      if (!hud_engage_drone(i, &d)) continue;
      float b;
      if (d.loc) {
        double dist = geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b);
        double rr = (dist / maxR) * R; if (rr > R) rr = R;
        int bx = cx + (int)(sinf(b * 0.01745f) * rr), by = cy - (int)(cosf(b * 0.01745f) * rr);
        hud_disc(bx, by, 3, HUD_C_RED); hud_ring(bx, by, 5, HUD_C_RED);
      }
      if (d.op) {                                   // the pilot, from the drone's own broadcast
        double dist = geo_dist_brg(g.lat, g.lon, d.oplat, d.oplon, &b);
        double rr = (dist / maxR) * R; if (rr > R) rr = R;
        int px = cx + (int)(sinf(b * 0.01745f) * rr), py = cy - (int)(cosf(b * 0.01745f) * rr);
        hud_line(px - 3, py, px + 3, py, HUD_C_AMBER); hud_line(px, py - 3, px, py + 3, HUD_C_AMBER);
      }
    }
  // teammates (blue force) at true bearing + range -- green
  if (g.valid)
    for (int i = 0; i < fn; i++) {
      if (!hud_comms_friend(i, &fi)) continue;
      float b; double dist = geo_dist_brg(g.lat, g.lon, fi.lat, fi.lon, &b);
      double rr = (dist / maxR) * R; if (rr > R) rr = R;
      int fx = cx + (int)(sinf(b * 0.01745f) * rr), fy = cy - (int)(cosf(b * 0.01745f) * rr);
      hud_disc(fx, fy, 3, HUD_C_GREEN); hud_ring(fx, fy, 5, HUD_C_GREEN);
    }
  hud_disc(cx, cy, 2, HUD_C_WHITE);                 // you

  // readout panel (in content, not the clipped strip)
  hud_fill_rect(0, CONTENT_B - 13, HUD_W, 13, HUD_C_STRIP);
  char ln[48];
  if (!g.valid) snprintf(ln, sizeof(ln), "DRONES %d   NO GPS", dn);
  else          snprintf(ln, sizeof(ln), "DRONES %d  TEAM %d  RANGE %dM", dn, fn, (int)maxR);
  hud_text(6, CONTENT_B - 10, ln, 1, dn ? HUD_C_RED : HUD_C_GREEN);
}

// ---- SCAN list cursor (highlighted row) + scroll window ----
static int s_sel = 0, s_scroll = 0;
#define ROW_H 20
static int scan_rows() { return (CONTENT_B - CONTENT_Y - FOOT_H) / ROW_H; }  // reserve the footer row

// Band filter: is this channel's band visible under the current filter?
static bool band_ok(uint8_t ch) {
  (void)ch; return true;   // single-band scan now: the list only holds the scanned band
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
  // footer: SCAN start/stop button (left) + band + AP count (right)
  hud_fill_rect(0, CONTENT_B - FOOT_H, HUD_W, FOOT_H, HUD_C_STRIP);
  bool en = hud_scan_enabled();
  const char* sb = en ? "SCANNING" : "PAUSED-TAP";
  hud_text(6, CONTENT_B - FOOT_H + 3, sb, 1, en ? HUD_C_GREEN : HUD_C_AMBER);
  char foot[20];
  snprintf(foot, sizeof(foot), "%s  %dAP", s_band == 2 ? "5G" : "2.4", hud_scan_count());
  hud_text(HUD_W - hud_text_w(foot, 1) - 6, CONTENT_B - FOOT_H + 3, foot, 1, HUD_C_CYAN);
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
static const double MAP_MPP[] = { 1, 3, 8, 25, 80 };   // metres/pixel zoom steps
#define MAP_ZOOM_N ((int)(sizeof(MAP_MPP) / sizeof(MAP_MPP[0])))
static int    s_mapZoom = 1;              // index into MAP_MPP (default 3 m/px)
static double s_panE = 0, s_panN = 0;     // view-centre offset from you: metres east / north
#define MAP_BTN_H 20                      // bottom button row height
#define MAP_BOT (CONTENT_B - MAP_BTN_H - 24)   // map area bottom (above 2 readout lines + buttons)
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

// MAP button row: [-] [+] [MARK] [SEND] [ME]
static const char* const MAP_BTN[] = { "-", "+", "MARK", "SEND", "ME" };
#define MAP_BTN_N 5
#define MAP_BTN_W (HUD_W / MAP_BTN_N)

static void page_map(uint32_t now) {
  const GpsFix& g = hud_gps();
  double mpp = MAP_MPP[s_mapZoom];
  double cosl = g.valid ? cos(g.lat * 0.01745) : 1.0;
  int cx = HUD_W / 2;
  int mapBot = MAP_BOT;
  int cy = (CONTENT_Y + mapBot) / 2;
  // project a geo point to screen (you-centred, with zoom + pan). true = inside the map area.
  auto proj = [&](double la, double lo, int& sx, int& sy) -> bool {
    double mE = (lo - g.lon) * 111320.0 * cosl;
    double mN = (la - g.lat) * 111320.0;
    sx = cx + (int)((mE - s_panE) / mpp);
    sy = cy - (int)((mN - s_panN) / mpp);
    return (sx >= 0 && sx < HUD_W && sy >= CONTENT_Y && sy < mapBot);
  };

  hud_fill_rect(0, CONTENT_Y, HUD_W, mapBot - CONTENT_Y, HUD_C_BG);
  // reference grid, shifted by the pan so panning reads as movement
  int offx = ((int)(-s_panE / mpp) % 32 + 32) % 32;
  int offy = ((int)(s_panN / mpp) % 32 + 32) % 32;
  for (int x = offx; x < HUD_W; x += 32) hud_fill_rect(x, CONTENT_Y, 1, mapBot - CONTENT_Y, HUD_C_DGREEN);
  for (int y = CONTENT_Y + offy; y < mapBot; y += 32) hud_fill_rect(0, y, HUD_W, 1, HUD_C_DGREEN);
  hud_text(HUD_W - 10, CONTENT_Y + 3, "N", 1, HUD_C_WHITE);

  // breadcrumb trail (record when moved > ~2 m)
  if (g.valid && (now - s_trLast > 1000)) {
    bool far = true;
    if (s_trCount > 0) {
      double dy = (g.lat - s_trLat[s_trCount - 1]) * 111320.0;
      double dx = (g.lon - s_trLon[s_trCount - 1]) * 111320.0 * cosl;
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
  int sx, sy;
  if (g.valid)
    for (int i = 0; i < s_trCount; i++)
      if (proj(s_trLat[i], s_trLon[i], sx, sy)) hud_disc(sx, sy, 1, HUD_C_AMBER);

  // moving? displacement over MOVE_MS -> speed + heading
  if (g.valid) {
    if (s_refT == 0) { s_refLat = g.lat; s_refLon = g.lon; s_refT = now; }
    uint32_t dt = now - s_refT;
    if (dt >= MOVE_MS) {
      double dy = (g.lat - s_refLat) * 111320.0;
      double dx = (g.lon - s_refLon) * 111320.0 * cosl;
      double dist = sqrt(dx * dx + dy * dy);
      if (dist > MOVE_M) {
        s_moving = true;
        s_spdMph = (float)(dist / (dt / 1000.0) * 2.23694);
        s_hdg = atan2f((float)dx, (float)dy) * 57.2958f; if (s_hdg < 0) s_hdg += 360;
      } else s_moving = false;
      s_refLat = g.lat; s_refLon = g.lon; s_refT = now;
    }
  } else { s_moving = false; s_refT = 0; }

  // teammates' shared MARKS (amber X + sender name)
  if (g.valid) {
    int mn = hud_comms_mark_count(); FriendInfo mk;
    for (int i = 0; i < mn; i++)
      if (hud_comms_mark(i, &mk) && proj(mk.lat, mk.lon, sx, sy)) {
        hud_line(sx - 4, sy - 4, sx + 4, sy + 4, HUD_C_AMBER);
        hud_line(sx - 4, sy + 4, sx + 4, sy - 4, HUD_C_AMBER);
        hud_text(sx + 5, sy - 3, mk.name, 1, HUD_C_AMBER);
      }
  }

  // YOUR dropped mark (cyan diamond)
  if (g.valid && s_wpSet && proj(s_wpLat, s_wpLon, sx, sy)) {
    hud_line(sx, sy - 4, sx + 4, sy, HUD_C_CYAN); hud_line(sx + 4, sy, sx, sy + 4, HUD_C_CYAN);
    hud_line(sx, sy + 4, sx - 4, sy, HUD_C_CYAN); hud_line(sx - 4, sy, sx, sy - 4, HUD_C_CYAN);
  }

  // teammates (blue force) -- green dot + name
  if (g.valid) {
    int fn = hud_comms_friend_count(); FriendInfo fi;
    for (int i = 0; i < fn; i++)
      if (hud_comms_friend(i, &fi) && proj(fi.lat, fi.lon, sx, sy)) {
        hud_disc(sx, sy, 2, HUD_C_GREEN);
        hud_text(sx + 4, sy - 3, fi.name, 1, HUD_C_GREEN);
      }
  }

  // you-marker: heading arrow, at your projected position (off-centre when panned)
  int yx = cx, yy = cy;
  if (g.valid) proj(g.lat, g.lon, yx, yy);
  float a = (s_moving ? s_hdg : 0.0f) * 0.01745329f;
  uint16_t mc = g.valid ? HUD_C_GREEN : HUD_C_GREY;
  if (yx >= -8 && yx < HUD_W + 8 && yy >= CONTENT_Y - 8 && yy < mapBot + 8) {
    int tx = yx + (int)(sinf(a) * 9), ty = yy - (int)(cosf(a) * 9);
    int lx2 = yx + (int)(sinf(a + 2.6f) * 7), ly2 = yy - (int)(cosf(a + 2.6f) * 7);
    int rx2 = yx + (int)(sinf(a - 2.6f) * 7), ry2 = yy - (int)(cosf(a - 2.6f) * 7);
    hud_line(tx, ty, lx2, ly2, mc); hud_line(tx, ty, rx2, ry2, mc); hud_line(lx2, ly2, rx2, ry2, mc);
  }

  // centre crosshair = where MARK drops (the view centre)
  hud_line(cx - 5, cy, cx + 5, cy, HUD_C_WHITE); hud_line(cx, cy - 5, cx, cy + 5, HUD_C_WHITE);

  // ---- readout (two lines, above the buttons) ----
  int ry1 = CONTENT_B - MAP_BTN_H - 22, ry2 = CONTENT_B - MAP_BTN_H - 11;
  hud_fill_rect(0, ry1 - 2, HUD_W, 22, HUD_C_STRIP);
  char ln[48];
  if (g.valid) {
    if (s_wpSet) {                                                 // line 1: position or mark nav
      float wb; double wd = geo_dist_brg(g.lat, g.lon, s_wpLat, s_wpLon, &wb);
      snprintf(ln, sizeof(ln), "MARK %dM %03d %s", (int)wd, (int)wb, cardinal(wb));
    } else snprintf(ln, sizeof(ln), "%.5f %.5f", g.lat, g.lon);
    hud_text(6, ry1, ln, 1, s_wpSet ? HUD_C_CYAN : HUD_C_GREEN);
    if (s_moving)                                                  // line 2: heading/speed or status
      snprintf(ln, sizeof(ln), "HDG %03d %s  %.1f MPH  SAT %d", (int)s_hdg, cardinal(s_hdg), s_spdMph, g.sats);
    else
      snprintf(ln, sizeof(ln), "STOPPED  SAT %d  ALT %dM", g.sats, (int)g.altm);
    hud_text(6, ry2, ln, 1, HUD_C_GREY);
  } else {
    hud_text(6, ry1, "ACQUIRING GPS...", 1, HUD_C_AMBER);
    snprintf(ln, sizeof(ln), "SAT %d   NO FIX", g.sats);
    hud_text(6, ry2, ln, 1, HUD_C_GREY);
  }
  char zl[12]; snprintf(zl, sizeof(zl), "%dm/px", (int)mpp);        // scale, right-aligned on line 1
  hud_text(HUD_W - hud_text_w(zl, 1) - 4, ry1, zl, 1, HUD_C_GREY);

  // ---- button row ----
  int by = CONTENT_B - MAP_BTN_H;
  bool canSend = s_wpSet && !hud_comms_net_open();
  for (int i = 0; i < MAP_BTN_N; i++) {
    int bx = i * MAP_BTN_W;
    uint16_t c = (i == 3) ? (canSend ? HUD_C_GREEN : HUD_C_GREY)
               : (i == 2) ? HUD_C_CYAN : (i == 4) ? HUD_C_AMBER : HUD_C_CYAN;
    hud_fill_rect(bx, by, MAP_BTN_W - 1, MAP_BTN_H, HUD_C_STRIP);
    hud_text(bx + (MAP_BTN_W - hud_text_w(MAP_BTN[i], 1)) / 2, by + (MAP_BTN_H - 7) / 2, MAP_BTN[i], 1, c);
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
  if (hud_comms_beaconing())                       // blue-force position beacon is live
    hud_text(HUD_W - hud_text_w("[SET]", 1) - hud_text_w("BF", 1) - 12, NET_Y, "BF", 1, HUD_C_GREEN);

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

// ---- full-screen DRONE ALARM: a loud takeover of the content band when a drone
// appears, headlined by the nearest drone's range/bearing and the PILOT's bearing.
// A tap acknowledges it (drops back to the small strip banner); re-arms when clear.
static bool s_droneAck = false;
static void draw_drone_alarm() {
  int dn = hud_engage_drone_count();
  const GpsFix& g = hud_gps();
  bool flash = (millis() / 300) & 1;
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, flash ? HUD_C_RED : HUD_C_BG);
  uint16_t fg = flash ? HUD_C_BG : HUD_C_RED;
  int y = CONTENT_Y + 14;
  hud_text(cen("DRONE", 3), y, "DRONE", 3, fg);         y += 30;
  hud_text(cen("DETECTED", 2), y, "DETECTED", 2, fg);   y += 26;
  char ln[40];
  snprintf(ln, sizeof(ln), "COUNT %d", dn);
  hud_text(cen(ln, 2), y, ln, 2, fg);                   y += 30;
  if (g.valid) {
    double best = 1e18; float db = 0; int bi = -1;
    DroneInfo d, nd = {};
    for (int i = 0; i < dn; i++) {
      if (!hud_engage_drone(i, &d) || !d.loc) continue;
      float b; double dist = geo_dist_brg(g.lat, g.lon, d.lat, d.lon, &b);
      if (dist < best) { best = dist; db = b; bi = i; nd = d; }
    }
    if (bi >= 0) {
      snprintf(ln, sizeof(ln), "DRONE %dM  %03d", (int)best, (int)db);
      hud_text(cen(ln, 2), y, ln, 2, fg);               y += 26;
      if (nd.op) {
        float pb; double pd = geo_dist_brg(g.lat, g.lon, nd.oplat, nd.oplon, &pb);
        snprintf(ln, sizeof(ln), "PILOT %dM  %03d", (int)pd, (int)pb);
        hud_text(cen(ln, 2), y, ln, 2, fg);
      } else {
        hud_text(cen("PILOT: NO SIGNAL YET", 1), y, "PILOT: NO SIGNAL YET", 1, fg);
      }
    } else {
      hud_text(cen("LOCATION PENDING", 1), y, "LOCATION PENDING", 1, fg);
    }
  } else {
    hud_text(cen("NO GPS - NO RANGE", 1), y, "NO GPS - NO RANGE", 1, fg);
  }
  hud_text(cen("TAP TO ACK", 1), CONTENT_B - 14, "TAP TO ACK", 1, fg);
}

// ---- SD FILE MANAGER (modal over the content band): list /sd, select, delete ----
#define FM_MAX   32
#define FM_ROW_H 15
#define FM_LIST_Y (CONTENT_Y + 20)
#define FM_BTN_Y  (CONTENT_B - 22)
static bool     s_files = false;           // file manager open
static char     s_fmName[FM_MAX][24];
static uint32_t s_fmSize[FM_MAX];
static int      s_fmN = 0, s_fmSel = 0, s_fmScroll = 0;
static bool     s_fmConfirm = false;       // DELETE armed (2-tap confirm)

static void fm_refresh() {
  s_fmN = hud_sd_ok() ? hud_sd_list(s_fmName, s_fmSize, FM_MAX) : 0;
  if (s_fmSel >= s_fmN) s_fmSel = s_fmN > 0 ? s_fmN - 1 : 0;
  if (s_fmScroll > s_fmSel) s_fmScroll = s_fmSel;
  s_fmConfirm = false;
}
static int fm_rows() { return (FM_BTN_Y - 4 - FM_LIST_Y) / FM_ROW_H; }

static void fm_size_str(uint32_t b, char* out, int n) {
  if (b >= 1048576) snprintf(out, n, "%luM", (unsigned long)(b / 1048576));
  else if (b >= 1024) snprintf(out, n, "%luK", (unsigned long)(b / 1024));
  else snprintf(out, n, "%luB", (unsigned long)b);
}

static void draw_files() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  if (!hud_sd_ok()) {
    hud_text(cen("NO SD / UNREADABLE", 1), CONTENT_Y + 30, "NO SD / UNREADABLE", 1, HUD_C_RED);
    hud_text(cen("reseat the card, or:", 1), CONTENT_Y + 46, "reseat the card, or:", 1, HUD_C_GREY);
    hud_rect(SET_BTN_X, SET_BRT_Y, SET_BTN_W, 22, HUD_C_RED);
    const char* fb = s_fmConfirm ? "CONFIRM FORMAT (WIPES ALL)" : "FORMAT CARD -> FAT32";
    hud_text(HUD_W / 2 - hud_text_w(fb, 1) / 2, SET_BRT_Y + 7, fb, 1, HUD_C_RED);
    hud_text(cen("TAP ELSEWHERE TO CLOSE", 1), CONTENT_B - 14, "TAP ELSEWHERE TO CLOSE", 1, HUD_C_GREY);
    return;
  }
  char hdr[24]; snprintf(hdr, sizeof(hdr), "SD FILES  %d", s_fmN);
  hud_text(6, CONTENT_Y + 6, hdr, 1, HUD_C_WHITE);
  char cap[16]; snprintf(cap, sizeof(cap), "%luMB", (unsigned long)hud_sd_size_mb());
  hud_text(HUD_W - hud_text_w(cap, 1) - 6, CONTENT_Y + 6, cap, 1, HUD_C_GREY);

  if (s_fmN == 0) hud_text(8, FM_LIST_Y + 6, "(no files)", 1, HUD_C_GREY);
  int rows = fm_rows();
  for (int r = 0; r < rows; r++) {
    int idx = s_fmScroll + r;
    if (idx >= s_fmN) break;
    int y = FM_LIST_Y + r * FM_ROW_H;
    if (idx == s_fmSel) hud_fill_rect(0, y - 1, HUD_W, FM_ROW_H - 1, HUD_C_STRIP);
    hud_text(6, y + 2, s_fmName[idx], 1, idx == s_fmSel ? HUD_C_WHITE : HUD_C_GREY);
    char sz[12]; fm_size_str(s_fmSize[idx], sz, sizeof(sz));
    hud_text(HUD_W - hud_text_w(sz, 1) - 6, y + 2, sz, 1, HUD_C_GREY);
  }
  // VIEW | DELETE | CLOSE (three columns)
  int bw = HUD_W / 3;
  uint16_t vc = s_fmN ? HUD_C_CYAN : HUD_C_GREY;
  hud_rect(2, FM_BTN_Y, bw - 4, 18, vc);
  hud_text(bw / 2 - hud_text_w("VIEW", 1) / 2, FM_BTN_Y + 6, "VIEW", 1, vc);
  uint16_t dc = s_fmN ? HUD_C_RED : HUD_C_GREY;
  const char* del = s_fmConfirm ? "CONFIRM" : "DELETE";
  hud_rect(bw + 2, FM_BTN_Y, bw - 4, 18, dc);
  hud_text(bw + bw / 2 - hud_text_w(del, 1) / 2, FM_BTN_Y + 6, del, 1, dc);
  hud_rect(2 * bw + 2, FM_BTN_Y, bw - 4, 18, HUD_C_CYAN);
  hud_text(2 * bw + bw / 2 - hud_text_w("CLOSE", 1) / 2, FM_BTN_Y + 6, "CLOSE", 1, HUD_C_CYAN);
}

// ---- LOG / FILE VIEWER (shows the recent end of a file so you can decide before delete) ----
#define VIEW_BUF   4096
#define VIEW_LINES 220
static bool  s_view = false;
static char  s_vbuf[VIEW_BUF];
static char* s_vline[VIEW_LINES];
static int   s_vlines = 0, s_vscroll = 0;

static void view_open(const char* name) {
  s_vlines = 0; s_vscroll = 0;
  int n = hud_sd_read_tail(name, s_vbuf, VIEW_BUF);
  if (n <= 0) return;
  int start = 0;
  if (n >= VIEW_BUF - 1)                              // tail may start mid-line: skip to the next newline
    for (int i = 0; i < n; i++) if (s_vbuf[i] == '\n') { start = i + 1; break; }
  bool fresh = true;
  for (int i = start; i < n && s_vlines < VIEW_LINES; i++) {
    if (fresh) { s_vline[s_vlines++] = &s_vbuf[i]; fresh = false; }
    if (s_vbuf[i] == '\n' || s_vbuf[i] == '\r') { s_vbuf[i] = 0; fresh = true; }
  }
}

static void draw_view() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  hud_text(6, CONTENT_Y + 4, s_fmName[s_fmSel], 1, HUD_C_WHITE);
  char pos[16]; snprintf(pos, sizeof(pos), "%d/%d", s_vscroll + 1, s_vlines);
  hud_text(HUD_W - hud_text_w(pos, 1) - 6, CONTENT_Y + 4, pos, 1, HUD_C_GREY);
  int y0 = CONTENT_Y + 16, rh = 10;
  int rows = (CONTENT_B - 14 - y0) / rh;
  if (s_vlines == 0) hud_text(6, y0, "(empty)", 1, HUD_C_GREY);
  for (int r = 0; r < rows; r++) {
    int li = s_vscroll + r;
    if (li >= s_vlines) break;
    hud_text(4, y0 + r * rh, s_vline[li], 1, HUD_C_GREEN);
  }
  hud_text(cen("TOP/BOT=SCROLL  MID=CLOSE", 1), CONTENT_B - 11, "TOP/BOT=SCROLL  MID=CLOSE", 1, HUD_C_GREY);
}

// ---- DEVICE INFO + GPS status (modal; tap to close) ----
static bool s_info = false;
static void draw_info() {
  hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
  int y = CONTENT_Y + 6;
  char l[44];
  hud_text(cen(HUD_PRODUCT, 2), y, HUD_PRODUCT, 2, HUD_C_RED); y += 20;
  snprintf(l, sizeof(l), "%s v.%s", HUD_BUILD, HUD_VERSION); hud_text(cen(l, 1), y, l, 1, HUD_C_AMBER); y += 12;
  snprintf(l, sizeof(l), "BY %s", HUD_AUTHOR); hud_text(cen(l, 1), y, l, 1, HUD_C_GREY); y += 16;
  snprintf(l, sizeof(l), "UNIT  %s", hud_comms_name()); hud_text(8, y, l, 1, HUD_C_WHITE); y += 13;
  snprintf(l, sizeof(l), "RAM   %u KB free", (unsigned)(ESP.getFreeHeap() / 1024)); hud_text(8, y, l, 1, HUD_C_GREY); y += 13;
  uint32_t up = millis() / 1000;
  snprintf(l, sizeof(l), "UP    %luh %lum %lus", (unsigned long)(up / 3600), (unsigned long)((up / 60) % 60), (unsigned long)(up % 60));
  hud_text(8, y, l, 1, HUD_C_GREY); y += 13;
  if (hud_sd_ok()) snprintf(l, sizeof(l), "SD    OK  %luMB", (unsigned long)hud_sd_size_mb());
  else             snprintf(l, sizeof(l), "SD    none");
  hud_text(8, y, l, 1, hud_sd_ok() ? HUD_C_GREEN : HUD_C_GREY); y += 16;
  const GpsFix& g = hud_gps();
  hud_text(8, y, "-- GPS --", 1, HUD_C_WHITE); y += 13;
  snprintf(l, sizeof(l), "FIX %s  SAT %d  HDOP %.1f", g.valid ? "YES" : "no", g.sats, g.hdop);
  hud_text(8, y, l, 1, g.valid ? HUD_C_GREEN : HUD_C_AMBER); y += 13;
  if (g.valid) {
    snprintf(l, sizeof(l), "%.5f %.5f", g.lat, g.lon); hud_text(8, y, l, 1, HUD_C_GREEN); y += 13;
    snprintf(l, sizeof(l), "ALT %dM  UTC %s", (int)g.altm, g.utc[0] ? g.utc : "--"); hud_text(8, y, l, 1, HUD_C_GREY); y += 13;
  }
  snprintf(l, sizeof(l), "GPS RX %lu bytes", (unsigned long)hud_gps_rxbytes()); hud_text(8, y, l, 1, HUD_C_GREY);
  hud_text(cen("TAP TO CLOSE", 1), CONTENT_B - 12, "TAP TO CLOSE", 1, HUD_C_GREY);
}

void hud_page_draw(int mode, uint32_t now) {
  if (s_view) {
    draw_view();       // log / file viewer (modal)
  } else if (s_info) {
    draw_info();       // device info + GPS status (modal)
  } else if (s_files) {
    draw_files();      // SD file manager (modal)
  } else if (s_detail) {
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
  if (hud_engage_drone_count() == 0) s_droneAck = false;      // re-arm when the sky is clear
  else if (!s_droneAck) draw_drone_alarm();                   // loud takeover until acknowledged
}

// Which scan zone is y in? -1 = scroll up, +1 = scroll down, 0 = centre/enter.
static int scan_zone(int y) {
  if (y >= CONTENT_B - FOOT_H) return 2;            // footer strip = SCAN start/stop
  int bot = CONTENT_B - FOOT_H;                     // list area excludes the footer
  int band = bot - CONTENT_Y;
  if (y < CONTENT_Y + band / 3) return -1;          // upper third = scroll up
  if (y > bot - band / 3) return +1;                // lower third = scroll down
  return 0;                                         // middle = ENTER
}

// MAP controls: the button row (press only) + edge-pan (press or hold).
static void map_touch(int x, int y, bool repeat) {
  int mapBot = MAP_BOT;
  int by = CONTENT_B - MAP_BTN_H;
  if (!repeat && y >= by) {                          // button row
    int b = x / MAP_BTN_W;
    const GpsFix& g = hud_gps();
    double cosl = g.valid ? cos(g.lat * 0.01745) : 1.0;
    if (b == 0) { if (s_mapZoom < MAP_ZOOM_N - 1) s_mapZoom++; }     // [-] zoom out
    else if (b == 1) { if (s_mapZoom > 0) s_mapZoom--; }            // [+] zoom in
    else if (b == 2) {                                             // [MARK] at the view centre
      if (g.valid) { s_wpLat = g.lat + s_panN / 111320.0;
                     s_wpLon = g.lon + s_panE / (111320.0 * cosl); s_wpSet = true; }
    } else if (b == 3) {                                           // [SEND] the mark on the net
      if (s_wpSet && !hud_comms_net_open()) hud_comms_send_mark(s_wpLat, s_wpLon);
    } else if (b == 4) { s_panE = 0; s_panN = 0; }                 // [ME] recentre on yourself
    return;
  }
  if (y < CONTENT_Y || y >= mapBot) return;          // map area: edge-pan toward the tap
  int cx = HUD_W / 2, cy = (CONTENT_Y + mapBot) / 2;
  double step = 40.0 * MAP_MPP[s_mapZoom];
  if (x > cx + 20) s_panE += step; else if (x < cx - 20) s_panE -= step;
  if (y < cy - 20) s_panN += step; else if (y > cy + 20) s_panN -= step;
}

void hud_on_press(int x, int y) {
  // drone alarm is modal: the first tap just acknowledges it
  if (hud_engage_drone_count() > 0 && !s_droneAck) { s_droneAck = true; return; }

  if (s_view) {                                 // log/file viewer modal: top/bot scroll, mid closes
    int third = (CONTENT_B - CONTENT_Y) / 3;
    if (y < CONTENT_Y + third) s_vscroll -= 3;
    else if (y > CONTENT_B - third) s_vscroll += 3;
    else { s_view = false; return; }
    if (s_vscroll < 0) s_vscroll = 0;
    if (s_vscroll >= s_vlines) s_vscroll = s_vlines > 0 ? s_vlines - 1 : 0;
    return;
  }

  if (s_info) { s_info = false; return; }       // device info modal: any tap closes

  if (s_files) {                                // SD file manager modal
    if (!hud_sd_ok()) {                         // no card: offer FORMAT (two-tap), else close
      if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W && y >= SET_BRT_Y && y <= SET_BRT_Y + 22) {
        if (!s_fmConfirm) s_fmConfirm = true;
        else {
          hud_fill_rect(0, CONTENT_Y, HUD_W, CONTENT_B - CONTENT_Y, HUD_C_BG);
          hud_text(cen("FORMATTING SD...", 2), CONTENT_Y + 50, "FORMATTING SD...", 2, HUD_C_AMBER);
          hud_text(cen("WAIT ~1-2 MIN", 1), CONTENT_Y + 74, "WAIT ~1-2 MIN", 1, HUD_C_GREY);
          hud_present_fb(hud_framebuffer(), HUD_W, HUD_H);   // show it before the blocking format
          hud_sd_format();
          s_fmConfirm = false;
          fm_refresh();
        }
      } else { s_files = false; s_fmConfirm = false; }
      return;
    }
    if (y >= FM_BTN_Y) {
      int bw = HUD_W / 3, col = x / bw;         // 0 = VIEW, 1 = DELETE, 2 = CLOSE
      if (col == 0) { if (s_fmN > 0) { view_open(s_fmName[s_fmSel]); s_view = true; } }
      else if (col == 1) {                      // DELETE (two-tap confirm)
        if (s_fmN > 0) {
          if (!s_fmConfirm) s_fmConfirm = true;
          else { hud_sd_remove(s_fmName[s_fmSel]); fm_refresh(); }
        }
      } else s_files = false;                   // CLOSE
    } else if (y >= FM_LIST_Y) {                // tap a row to select (+ edge-scroll)
      int r = (y - FM_LIST_Y) / FM_ROW_H, idx = s_fmScroll + r;
      if (idx >= 0 && idx < s_fmN) { s_fmSel = idx; s_fmConfirm = false; }
      if (r == 0 && s_fmScroll > 0) s_fmScroll--;
      int rows = fm_rows();
      if (r >= rows - 1 && s_fmScroll + rows < s_fmN) s_fmScroll++;
    }
    return;
  }

  // settings gear (top-right of the status strip) toggles the SETTINGS panel
  if (y < STAT_H && x > HUD_W - 24) { s_settings = !s_settings; s_detail = false; return; }

  if (s_detail) { s_detail = false; return; } // info view: any tap closes it

  if (s_settings) {                           // panel open = modal
    if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
        y >= SET_BTN_Y && y <= SET_BTN_Y + SET_BTN_H) {
      hud_request_recal(); s_settings = false; // RECALIBRATE TOUCH
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_BND_Y && y <= SET_BND_Y + SET_BTN_H) {
      s_band = (s_band == 1) ? 2 : 1;         // BAND: 2.4 <-> 5 (controls the scan band)
      hud_scan_set_band(s_band == 2 ? 5 : 2);
      s_sel = 0; s_scroll = 0;
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_STL_Y && y <= SET_STL_Y + SET_BTN_H) {
      hud_set_stealth(!hud_stealth());        // NIGHT/STEALTH toggle (stay in panel)
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_BRT_Y && y <= SET_BRT_Y + SET_BTN_H) {
      uint8_t c = hud_brightness();           // BRIGHTNESS cycle 100->63->35->16%
      hud_set_brightness(c > 200 ? 160 : c > 120 ? 90 : c > 60 ? 40 : 255);
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_FIL_Y && y <= SET_FIL_Y + SET_BTN_H) {
      s_files = true; s_settings = false; fm_refresh();   // open SD file manager (works even with no card -> FORMAT)
    } else if (x >= SET_BTN_X && x <= SET_BTN_X + SET_BTN_W &&
               y >= SET_INF_Y && y <= SET_INF_Y + SET_BTN_H) {
      s_info = true; s_settings = false;      // open DEVICE INFO + GPS
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
    else if (z == 2) hud_scan_set_enabled(!hud_scan_enabled());   // footer = SCAN start/stop
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
    map_touch(x, y, false);                    // zoom / mark / send / recentre / pan
  }
}

void hud_on_repeat(int x, int y) {
  if (s_view) {                                // hold to keep scrolling the viewer
    int third = (CONTENT_B - CONTENT_Y) / 3;
    if (y < CONTENT_Y + third) s_vscroll -= 2;
    else if (y > CONTENT_B - third) s_vscroll += 2;
    if (s_vscroll < 0) s_vscroll = 0;
    if (s_vscroll >= s_vlines) s_vscroll = s_vlines > 0 ? s_vlines - 1 : 0;
    return;
  }
  if (s_settings || s_detail || s_files || s_info) return;  // no auto-repeat while a panel is open
  if (y >= HUD_H - TAB_H) return;             // tabs/enter don't auto-repeat
  if (hud_mode_get() == M_SCAN) {
    int z = scan_zone(y);
    if (z == -1) scan_move(-1);
    else if (z == +1) scan_move(+1);
  } else if (hud_mode_get() == M_MAP) {
    map_touch(x, y, true);                     // hold to keep panning
  }
}
