#pragma once
#include <stdint.h>
// Direction-finding for the locked SCAN target: as you sweep a directional antenna,
// the target's RSSI peaks when you point AT it. We bin the target's RSSI by compass
// heading; the fullest bin is the estimated bearing. Bins decay so the arrow re-
// converges as you sweep again. Needs a locked target + the compass present.
// (RSSI comes from the scan, so it updates per scan cycle -- sweep slowly.)
void   hud_df_begin();
void   hud_df_tick(uint32_t now);
bool   hud_df_active();            // target locked AND compass present
float  hud_df_bearing();           // estimated bearing to target (deg 0..360), or -1
int8_t hud_df_peak_rssi();         // strongest RSSI seen (dBm), or -127
void   hud_df_reset();             // clear the sweep (start over)
