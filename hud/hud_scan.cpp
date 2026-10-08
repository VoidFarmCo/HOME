#include "hud_scan.h"
#include <WiFi.h>
#include <string.h>

// ---- live RF contacts ----
// WiFi scan is ASYNC: WiFi.scanNetworks(true,...) returns immediately and the
// radio scans on its own task; the render loop keeps running at ~29 FPS. We only
// poll WiFi.scanComplete() here. (BLE is added as a second, time-sliced phase in
// a later increment -- the C5 is single-core with ONE 2.4 GHz radio, so WiFi and
// BLE can't own the air at the same time.)

static Contact  s_list[HUD_SCAN_MAX];
static int      s_count   = 0;
static int      s_wifi_n  = 0;
static int      s_ble_n   = 0;
static bool     s_scanning = false;
static uint32_t s_next    = 0;      // next time we're allowed to start a scan

void hud_scan_begin() {
  WiFi.mode(WIFI_STA);              // station mode = able to scan, not an AP
  WiFi.disconnect(false, true);     // don't join anything; forget stored creds in RAM
}

// Strongest-first so the top of the SCAN list and the inner radar rings show the
// closest contacts. Small N (<=24) -> a plain selection sort is fine.
static void sort_by_rssi() {
  for (int i = 0; i < s_count - 1; i++) {
    int best = i;
    for (int j = i + 1; j < s_count; j++)
      if (s_list[j].rssi > s_list[best].rssi) best = j;
    if (best != i) { Contact t = s_list[i]; s_list[i] = s_list[best]; s_list[best] = t; }
  }
}

void hud_scan_tick(uint32_t now) {
  if (!s_scanning) {
    if ((int32_t)(now - s_next) >= 0) {
      WiFi.scanNetworks(true /*async*/, true /*show hidden*/);
      s_scanning = true;
    }
    return;
  }

  int r = WiFi.scanComplete();
  if (r == WIFI_SCAN_RUNNING) return;             // still scanning on the radio task
  if (r == WIFI_SCAN_FAILED) {                    // couldn't start -- back off and retry
    s_scanning = false;
    s_next = now + 1000;
    return;
  }

  // r >= 0: results are ready. Copy out, then free the driver's copy.
  int n = (r > HUD_SCAN_MAX) ? HUD_SCAN_MAX : r;
  for (int i = 0; i < n; i++) {
    String ss = WiFi.SSID(i);
    const char* nm = ss.length() ? ss.c_str() : "(hidden)";
    strncpy(s_list[i].name, nm, sizeof(s_list[i].name) - 1);
    s_list[i].name[sizeof(s_list[i].name) - 1] = 0;
    s_list[i].rssi = (int8_t)WiFi.RSSI(i);
    s_list[i].ch   = (uint8_t)WiFi.channel(i);
    s_list[i].type = CT_WIFI;
  }
  s_wifi_n = n;
  s_count  = n;
  sort_by_rssi();
  WiFi.scanDelete();
  s_scanning = false;
  s_next = now + 1500;                            // re-scan ~every 1.5 s after finishing
}

int            hud_scan_count()      { return s_count; }
const Contact* hud_scan_list()       { return s_list; }
int            hud_scan_wifi_count() { return s_wifi_n; }
int            hud_scan_ble_count()  { return s_ble_n; }
