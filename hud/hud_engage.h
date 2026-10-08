#pragma once
#include <stdint.h>

// ENGAGE (threat detect): passive WiFi sniffer, hopping 2.4 GHz. Two threats:
//   1) deauth/disassoc attacks (jamming nearby WiFi) -- counted.
//   2) DRONES broadcasting Open Drone ID / Remote ID in WiFi beacons (ASTM F3411) --
//      decoded to the drone's ID + its own GPS position + the operator's position.
// RX only (no transmit). One radio -> the WiFi scanner is paused on the ENGAGE page.

struct DroneInfo {
  char    id[21];        // UAS ID (serial)
  double  lat, lon;      // drone position (0 if not yet heard)
  double  oplat, oplon;  // operator position (0 if not broadcast)
  int8_t  rssi;
  bool    loc, op;       // have drone loc / operator loc
};

void     hud_engage_enter();
void     hud_engage_leave();
void     hud_engage_tick(uint32_t now);
uint32_t hud_engage_frames();
uint32_t hud_engage_deauth();
uint32_t hud_engage_last_ms();
int      hud_engage_channel();
int      hud_engage_drone_count();            // drones currently tracked
bool     hud_engage_drone(int i, DroneInfo* out);
