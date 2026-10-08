#pragma once
#include <stdint.h>

// Live RF contacts for the SCAN/RADAR pages. Scanning is ASYNC (the single-core
// C5 renders at ~29 FPS; a blocking scan would freeze the HUD), so hud_scan_tick
// just drives the state machine and the pages read the latest snapshot.

enum ContactType { CT_WIFI = 0, CT_BLE = 1 };

struct Contact {
  char    name[24];
  int8_t  rssi;      // dBm
  uint8_t ch;        // wifi channel (0 for BLE)
  uint8_t type;      // ContactType
};

void           hud_scan_begin();            // bring up the radios (call in setup)
void           hud_scan_tick(uint32_t now); // drive async scans; non-blocking
int            hud_scan_count();            // contacts in the current snapshot
const Contact* hud_scan_list();             // the snapshot (<= HUD_SCAN_MAX)
int            hud_scan_wifi_count();        // how many are wifi (for status)
int            hud_scan_ble_count();

// ---- fox-hunt target (lock one contact and track its live signal) ----
void           hud_scan_set_target(const char* name);  // lock this contact as the hunt target
void           hud_scan_clear_target();
bool           hud_scan_has_target();
const char*    hud_scan_target_name();
int            hud_scan_target_rssi();       // live RSSI of the target (dBm), or -127 if lost
bool           hud_scan_target_seen();       // was the target in the latest scan?

#define HUD_SCAN_MAX 24
