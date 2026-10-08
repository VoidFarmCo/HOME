#pragma once
#include <stdint.h>

// Live RF contacts for the SCAN/RADAR pages. Scanning is ASYNC (the single-core
// C5 renders at ~29 FPS; a blocking scan would freeze the HUD), so hud_scan_tick
// just drives the state machine and the pages read the latest snapshot.

enum ContactType { CT_WIFI = 0, CT_BLE = 1 };

struct Contact {
  char     name[24];
  int8_t   rssi;        // dBm
  uint8_t  ch;          // wifi channel (0 for BLE)
  uint8_t  type;        // ContactType
  uint8_t  bssid[6];    // AP MAC (the stable key)
  uint8_t  enc;         // wifi_auth_mode_t (0 = OPEN)
  uint32_t last_seen;   // millis() of the last scan that saw it (for aging out)
};

// 2 or 5 (GHz) from a wifi channel: ch 1..14 = 2.4, 32+ = 5.
static inline int hud_band_of(uint8_t ch) { return ch >= 15 ? 5 : 2; }
// Short encryption label for the info view (WPA2 etc.).
const char* hud_enc_name(uint8_t enc);

void           hud_scan_begin();            // bring up the radios (call in setup)
void           hud_scan_tick(uint32_t now); // drive async scans; non-blocking
int            hud_scan_count();            // contacts in the current snapshot
const Contact* hud_scan_list();             // the snapshot (<= HUD_SCAN_MAX)
int            hud_scan_wifi_count();        // total wifi contacts (both bands)
int            hud_scan_ble_count();
bool           hud_scan_ble_ready();         // did the BT controller come up?
int            hud_scan_band_count(int ghz);  // wifi contacts on 2 or 5 GHz

// ---- fox-hunt target (lock one contact and track its live signal) ----
void           hud_scan_set_target(const char* name);  // lock this contact as the hunt target
void           hud_scan_clear_target();
bool           hud_scan_has_target();
const char*    hud_scan_target_name();
int            hud_scan_target_rssi();       // live RSSI of the target (dBm), or -127 if lost
bool           hud_scan_target_seen();       // was the target in the latest scan?

#define HUD_SCAN_MAX 48   // holds a dense dual-band area (2.4 + 5 GHz) without truncating
