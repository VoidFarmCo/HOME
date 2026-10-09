#pragma once
#include <stdint.h>
// Field logging to the SD, gated by a single LOG on/off (the "log button").
// Separate files: threats.csv (drones + deauth), track.csv (GPS breadcrumb),
// comms.csv (chat/marks), wifi.csv (the TRACKED AP's seen/lost events only).
// Polling, non-blocking; writes only when logging is ON and the SD is mounted.
void     hud_log_tick(uint32_t now);
void     hud_log_set_enabled(bool en);
bool     hud_log_enabled();
uint32_t hud_log_rows();          // rows written this session
