#include "hud_scan.h"
#include <WiFi.h>
#include <string.h>

// ---- live WiFi contacts, DUAL-BAND (2.4 GHz + 5 GHz), PERSISTENT/TRACKED ----
// The ESP32-C5 is dual-band but its radio does ONE band at a time, so each cycle
// scans 2.4 GHz, switches the band, scans 5 GHz. The list is STABLE: an AP is kept
// in place and only its signal updates (keyed by BSSID), new APs are appended, and
// APs not seen for a while (gone) or too distant (below the add cutoff) drop off.
// No per-cycle re-sort -> the list doesn't jump under the cursor. Scans are async
// (WiFi.scanNetworks(true)) so they don't block the ~29 FPS loop.
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

enum Phase { P_2G_START, P_2G_RUN, P_5G_START, P_5G_RUN, P_WAIT };
static Phase    s_phase = P_2G_START;
static uint32_t s_next = 0;

void hud_scan_begin() {
  WiFi.mode(WIFI_STA);              // station mode = able to scan, not an AP
  WiFi.disconnect(false, true);     // don't join anything; forget stored creds in RAM
  s_phase = P_2G_START;
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
  switch (s_phase) {
    case P_2G_START:
      if ((int32_t)(now - s_next) < 0) break;
      WiFi.setBandMode(WIFI_BAND_MODE_2G_ONLY);
      WiFi.scanNetworks(true, true);             // async, show hidden
      s_phase = P_2G_RUN;
      break;

    case P_2G_RUN: {
      int r = WiFi.scanComplete();
      if (r == WIFI_SCAN_RUNNING) break;
      if (r >= 0) upsert_band(r, now);
      WiFi.scanDelete();
      s_phase = P_5G_START;
      break;
    }

    case P_5G_START:
      WiFi.setBandMode(WIFI_BAND_MODE_5G_ONLY);
      WiFi.scanNetworks(true, true);
      s_phase = P_5G_RUN;
      break;

    case P_5G_RUN: {
      int r = WiFi.scanComplete();
      if (r == WIFI_SCAN_RUNNING) break;
      if (r >= 0) upsert_band(r, now);
      WiFi.scanDelete();
      finish_cycle(now);
      s_phase = P_WAIT;
      s_next = now + 600;
      break;
    }

    case P_WAIT:
      if ((int32_t)(now - s_next) >= 0) s_phase = P_2G_START;
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
