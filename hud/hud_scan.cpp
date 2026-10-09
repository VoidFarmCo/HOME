#include "hud_scan.h"
#include <WiFi.h>
#include <string.h>

// ---- live WiFi contacts, DUAL-BAND (2.4 GHz + 5 GHz), PERSISTENT/TRACKED ----
// The ESP32-C5 is dual-band but its radio does ONE band at a time. Each cycle scans
// just ONE band, alternating 2.4 <-> 5 GHz, because WiFi.setBandMode + scanNetworks
// START blocks the loop ~100 ms on the C5 -- one switch per cycle (not two) spaced by
// SCAN_GAP keeps that hitch rare (SCAN is the only page that scans). The list is
// STABLE: an AP is kept in place and only its signal updates (keyed by BSSID), new
// APs are appended, and APs gone for AGE_MS (> 2 cycles, so the un-scanned band does
// not age out) or too distant (below ADD_RSSI) drop off. No per-cycle re-sort.
// (BLE is parked: the C5's BT controller crash-loops in arduino-esp32 3.3.12 -- a
// known upstream coexistence bug. The ble_* accessors stay, returning empty.)

static Contact  s_list[HUD_SCAN_MAX];    int s_count  = 0;   // persists across cycles
static int      s_wifi_n = 0, s_n24 = 0, s_n5 = 0;           // reported totals
#define AGE_MS    15000     // drop an AP not seen in this long (longer than one full
                            // dual-band cycle, so the first band doesn't age out mid-cycle)
#define ADD_RSSI  (-90)     // don't add APs weaker than this (too distant)

static char     s_target[24] = {0};
static int8_t   s_target_rssi = -127;
static bool     s_target_seen = false;

// SINGLE band, user-selected (no automatic 2.4<->5 switching -- that WiFi.setBandMode
// is what blocked the loop ~100 ms on the C5). We scan only s_band_want and call
// setBandMode ONLY when the user actually changes the band, so steady scanning does
// just a scanNetworks each cycle. The SCAN page is the only page that scans.
enum Phase { P_START, P_RUN, P_WAIT };
static Phase    s_phase = P_START;
static uint32_t s_next = 0;
static int      s_band_want = 2;    // band to scan (2 = 2.4 GHz, 5 = 5 GHz); set by the UI
static int      s_band_applied = -1;// last band actually set on the radio
#define SCAN_GAP 1200               // ms between scans

void hud_scan_set_band(int band) { s_band_want = (band == 5) ? 5 : 2; }

static bool s_scanEnabled = true;    // SCAN start/stop: when off, the list freezes (no scanNetworks hitch)
void hud_scan_set_enabled(bool en) { s_scanEnabled = en; }
bool hud_scan_enabled() { return s_scanEnabled; }

void hud_scan_begin() {
  WiFi.mode(WIFI_STA);              // station mode = able to scan, not an AP
  WiFi.disconnect(false, true);     // don't join anything; forget stored creds in RAM
  s_phase = P_START;
  s_next = 0;
}

const char* hud_enc_name(uint8_t enc) {
  switch (enc) {
    case WIFI_AUTH_OPEN:            return "OPEN";
    case WIFI_AUTH_WEP:             return "WEP";
    case WIFI_AUTH_WPA_PSK:         return "WPA";
    case WIFI_AUTH_WPA2_PSK:        return "WPA2";
    case WIFI_AUTH_WPA_WPA2_PSK:    return "WPA/2";
    case WIFI_AUTH_WPA2_ENTERPRISE: return "WPA2-E";
    case WIFI_AUTH_WPA3_PSK:        return "WPA3";
    case WIFI_AUTH_WPA2_WPA3_PSK:   return "WPA2/3";
    default:                        return "?";
  }
}

// Key by BSSID *and band*: a dual-band router uses the same MAC on 2.4 and 5 GHz,
// and we want to show both, so they're separate entries.
static int find_ap(const uint8_t* b, int band) {
  for (int i = 0; i < s_count; i++)
    if (hud_band_of(s_list[i].ch) == band && !memcmp(s_list[i].bssid, b, 6)) return i;
  return -1;
}

// Merge one band's scan results into the persistent list: update a known AP in
// place (keyed by BSSID), or append a new one. Never reorders existing entries.
static void upsert_band(int r, uint32_t now) {
  for (int i = 0; i < r; i++) {
    int8_t rssi = (int8_t)WiFi.RSSI(i);
    const uint8_t* b = WiFi.BSSID(i);
    if (!b) continue;
    int band = hud_band_of((uint8_t)WiFi.channel(i));
    int idx = find_ap(b, band);
    if (idx < 0) {                               // new AP (this band)
      if (rssi < ADD_RSSI) continue;             // too distant to bother adding
      if (s_count >= HUD_SCAN_MAX) continue;     // list full
      idx = s_count++;
      memcpy(s_list[idx].bssid, b, 6);
    }
    Contact& c = s_list[idx];
    String ss = WiFi.SSID(i);
    const char* nm = ss.length() ? ss.c_str() : "(hidden)";
    strncpy(c.name, nm, sizeof(c.name) - 1);
    c.name[sizeof(c.name) - 1] = 0;
    c.rssi = rssi;
    c.ch   = (uint8_t)WiFi.channel(i);
    c.type = CT_WIFI;
    c.enc  = (uint8_t)WiFi.encryptionType(i);
    c.last_seen = now;
  }
}

// After both bands: drop APs not seen for a while (gone), recount, refresh target.
static void finish_cycle(uint32_t now) {
  int w = 0;
  for (int i = 0; i < s_count; i++)
    if ((int32_t)(now - s_list[i].last_seen) < AGE_MS) {   // keep: recently seen
      if (w != i) s_list[w] = s_list[i];
      w++;
    }
  s_count = w;

  s_wifi_n = s_count;
  s_n24 = s_n5 = 0;
  for (int i = 0; i < s_count; i++) (hud_band_of(s_list[i].ch) == 5 ? s_n5 : s_n24)++;

  if (s_target[0]) {                              // refresh the locked target's live signal
    s_target_seen = false;
    for (int i = 0; i < s_count; i++)
      if (!strcmp(s_list[i].name, s_target)) { s_target_rssi = s_list[i].rssi; s_target_seen = true; break; }
  }
}

void hud_scan_tick(uint32_t now) {
  if (!s_scanEnabled && s_phase != P_RUN) return;   // paused: no new scans (list frozen), let a running one finish
  switch (s_phase) {
    case P_START:
      if (s_band_want != s_band_applied) {       // only switch when the user changed band
        WiFi.setBandMode(s_band_want == 5 ? WIFI_BAND_MODE_5G_ONLY : WIFI_BAND_MODE_2G_ONLY);
        s_band_applied = s_band_want;
      }
      WiFi.scanNetworks(true, true);             // async, show hidden
      s_phase = P_RUN;
      break;

    case P_RUN: {
      int r = WiFi.scanComplete();
      if (r == WIFI_SCAN_RUNNING) break;
      if (r >= 0) upsert_band(r, now);
      WiFi.scanDelete();
      finish_cycle(now);                         // recount whole list + age out
      s_phase = P_WAIT;
      s_next = now + SCAN_GAP;
      break;
    }

    case P_WAIT:
      if ((int32_t)(now - s_next) >= 0) s_phase = P_START;
      break;
  }
}

int            hud_scan_count()      { return s_count; }
const Contact* hud_scan_list()       { return s_list; }
int            hud_scan_wifi_count() { return s_wifi_n; }
int            hud_scan_ble_count()  { return 0; }       // BLE parked (C5 toolchain bug)
bool           hud_scan_ble_ready()  { return false; }
int            hud_scan_band_count(int ghz) { return ghz == 5 ? s_n5 : s_n24; }

void hud_scan_set_target(const char* name) {
  strncpy(s_target, name, sizeof(s_target) - 1);
  s_target[sizeof(s_target) - 1] = 0;
  s_target_rssi = -127; s_target_seen = false;
}
void        hud_scan_clear_target() { s_target[0] = 0; s_target_seen = false; }
bool        hud_scan_has_target()   { return s_target[0] != 0; }
const char* hud_scan_target_name()  { return s_target; }
int         hud_scan_target_rssi()  { return s_target_rssi; }
bool        hud_scan_target_seen()  { return s_target_seen; }
