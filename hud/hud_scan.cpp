#include "hud_scan.h"
#include <WiFi.h>
#include <BLEDevice.h>
#include <BLEScan.h>
#include <BLEAdvertisedDevice.h>
#include <string.h>

// ---- live RF contacts: WiFi + BLE, TIME-SLICED ----
// The C5 is single-core with ONE 2.4 GHz radio, so WiFi and BLE can't own the air
// at once. We run a phase machine: async WiFi scan -> async BLE scan -> combine ->
// short wait -> repeat. Neither scan blocks the ~29 FPS render loop.

static Contact  s_list[HUD_SCAN_MAX];    int s_count   = 0;
static Contact  s_wifi[HUD_SCAN_MAX];    int s_wifi_c  = 0;   // last WiFi snapshot
static Contact  s_bled[HUD_SCAN_MAX];    int s_bled_c  = 0;   // BLE, filled by callback
static int      s_wifi_n = 0, s_ble_n = 0;                    // reported counts

static char     s_target[24] = {0};
static int8_t   s_target_rssi = -127;
static bool     s_target_seen = false;

enum Phase { P_WIFI_IDLE, P_WIFI_RUN, P_BLE_RUN, P_WAIT };
static Phase    s_phase = P_WIFI_IDLE;
static uint32_t s_next = 0, s_bleStart = 0;
static BLEScan* s_bleScan = nullptr;
#define BLE_MS 2200

// Each BLE advertisement seen during a scan lands here (dedup by name/addr).
class HudBleCB : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice dev) override {
    if (s_bled_c >= HUD_SCAN_MAX) return;
    char buf[24];
    String nm = dev.haveName() ? dev.getName() : String();
    if (nm.length()) { strncpy(buf, nm.c_str(), sizeof(buf) - 1); buf[sizeof(buf) - 1] = 0; }
    else {
      String a = dev.getAddress().toString();           // aa:bb:cc:dd:ee:ff
      const char* s = a.c_str();
      int L = a.length();
      snprintf(buf, sizeof(buf), "BLE %s", L >= 5 ? s + L - 5 : s);  // last 5 chars
    }
    for (int i = 0; i < s_bled_c; i++) if (!strcmp(s_bled[i].name, buf)) return;  // dedupe
    strncpy(s_bled[s_bled_c].name, buf, sizeof(s_bled[s_bled_c].name) - 1);
    s_bled[s_bled_c].name[sizeof(s_bled[s_bled_c].name) - 1] = 0;
    s_bled[s_bled_c].rssi = (int8_t)dev.getRSSI();
    s_bled[s_bled_c].ch   = 0;
    s_bled[s_bled_c].type = CT_BLE;
    s_bled_c++;
  }
};
static HudBleCB s_bleCB;
static void ble_done(BLEScanResults) {}                  // presence makes start() async

void hud_scan_begin() {
  // BLE is DISABLED here on purpose: the ESP32-C5 core build ships the NimBLE
  // stack (CONFIG_BT_NIMBLE_ENABLED=y, Bluedroid OFF), but the BLEDevice library
  // used below is the Bluedroid one -> its controller init fails and crash-loops
  // the device, and even a failed btStart() disturbs the shared radio and kills
  // WiFi. Until the BLE code is ported to NimBLE, run WiFi-only (no btStart).
  s_bleScan = nullptr;

  WiFi.mode(WIFI_STA);              // station mode = able to scan, not an AP
  WiFi.disconnect(false, true);     // don't join anything; forget stored creds in RAM
}

static void sort_by_rssi() {
  for (int i = 0; i < s_count - 1; i++) {
    int best = i;
    for (int j = i + 1; j < s_count; j++)
      if (s_list[j].rssi > s_list[best].rssi) best = j;
    if (best != i) { Contact t = s_list[i]; s_list[i] = s_list[best]; s_list[best] = t; }
  }
}

// Merge the WiFi + BLE snapshots into the display list, strongest first.
static void combine(uint32_t now) {
  (void)now;
  int k = 0;
  for (int i = 0; i < s_wifi_c && k < HUD_SCAN_MAX; i++) s_list[k++] = s_wifi[i];
  for (int i = 0; i < s_bled_c && k < HUD_SCAN_MAX; i++) s_list[k++] = s_bled[i];
  s_count  = k;
  s_wifi_n = s_wifi_c;
  s_ble_n  = s_bled_c;
  sort_by_rssi();
  if (s_target[0]) {                              // refresh the locked target's live signal
    s_target_seen = false;
    for (int i = 0; i < s_count; i++)
      if (!strcmp(s_list[i].name, s_target)) { s_target_rssi = s_list[i].rssi; s_target_seen = true; break; }
  }
}

void hud_scan_tick(uint32_t now) {
  switch (s_phase) {
    case P_WIFI_IDLE:
      if ((int32_t)(now - s_next) >= 0) { WiFi.scanNetworks(true, true); s_phase = P_WIFI_RUN; }
      break;

    case P_WIFI_RUN: {
      int r = WiFi.scanComplete();
      if (r == WIFI_SCAN_RUNNING) break;
      if (r >= 0) {                                // copy WiFi results
        int n = (r > HUD_SCAN_MAX) ? HUD_SCAN_MAX : r;
        for (int i = 0; i < n; i++) {
          String ss = WiFi.SSID(i);
          const char* nm = ss.length() ? ss.c_str() : "(hidden)";
          strncpy(s_wifi[i].name, nm, sizeof(s_wifi[i].name) - 1);
          s_wifi[i].name[sizeof(s_wifi[i].name) - 1] = 0;
          s_wifi[i].rssi = (int8_t)WiFi.RSSI(i);
          s_wifi[i].ch   = (uint8_t)WiFi.channel(i);
          s_wifi[i].type = CT_WIFI;
        }
        s_wifi_c = n;
        WiFi.scanDelete();
      } else {
        s_wifi_c = 0;                              // scan failed -> no wifi this round
      }
      s_bled_c = 0;                                // start a fresh BLE window
      if (s_bleScan) {                             // BLE present -> scan it
        s_bleScan->clearResults();
        s_bleScan->start(BLE_MS / 1000 + 1, ble_done, false);
        s_bleStart = now;
        s_phase = P_BLE_RUN;
      } else {                                     // no BLE controller -> WiFi-only
        combine(now);
        s_phase = P_WAIT;
        s_next = now + 600;
      }
      break;
    }

    case P_BLE_RUN:
      if ((int32_t)(now - s_bleStart) >= BLE_MS) {
        if (s_bleScan) s_bleScan->stop();
        combine(now);
        s_phase = P_WAIT;
        s_next = now + 600;
      }
      break;

    case P_WAIT:
      if ((int32_t)(now - s_next) >= 0) s_phase = P_WIFI_IDLE;
      break;
  }
}

int            hud_scan_count()      { return s_count; }
const Contact* hud_scan_list()       { return s_list; }
int            hud_scan_wifi_count() { return s_wifi_n; }
int            hud_scan_ble_count()  { return s_ble_n; }
bool           hud_scan_ble_ready()  { return s_bleScan != nullptr; }

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
