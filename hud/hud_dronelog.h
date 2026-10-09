#pragma once
#include <stdint.h>
// Logs tracked Remote-ID drones to /sd/drones.csv every ~10 s while the SD is
// mounted and a drone is present. One row per drone per interval: timestamp (GPS
// UTC when available, else uptime), id, drone lat/lon, operator lat/lon, rssi, and
// your own lat/lon -- a time-series contact record you can pull off the card later.
void     hud_dronelog_tick(uint32_t now);
uint32_t hud_dronelog_rows();     // rows written this session
