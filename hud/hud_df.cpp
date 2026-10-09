#include "hud_df.h"
#include "hud_scan.h"
#include "hud_compass.h"

#define DF_BINS 24                 // 15 degrees per bin
static int8_t   s_bin[DF_BINS];
static uint32_t s_decayT = 0;

void hud_df_begin() { hud_df_reset(); }
void hud_df_reset() { for (int i = 0; i < DF_BINS; i++) s_bin[i] = -127; s_decayT = 0; }

bool hud_df_active() { return hud_scan_has_target() && hud_compass_present(); }

void hud_df_tick(uint32_t now) {
  if (!hud_df_active()) return;
  if (now - s_decayT > 1500) {     // slow decay so a re-sweep can move the peak
    s_decayT = now;
    for (int i = 0; i < DF_BINS; i++) if (s_bin[i] > -127) s_bin[i]--;
  }
  if (!hud_scan_target_seen()) return;
  float h = hud_compass_heading();
  if (h < 0) return;
  int8_t r = (int8_t)hud_scan_target_rssi();
  int b = ((int)(h / 15.0f)) % DF_BINS;
  if (b < 0) b += DF_BINS;
  if (r > s_bin[b]) s_bin[b] = r;  // strongest RSSI seen while facing this way
}

float hud_df_bearing() {
  int best = -1; int8_t bestr = -127;
  for (int i = 0; i < DF_BINS; i++) if (s_bin[i] > bestr) { bestr = s_bin[i]; best = i; }
  if (best < 0 || bestr <= -127) return -1.0f;
  return best * 15.0f + 7.5f;      // bin centre
}
int8_t hud_df_peak_rssi() {
  int8_t bestr = -127;
  for (int i = 0; i < DF_BINS; i++) if (s_bin[i] > bestr) bestr = s_bin[i];
  return bestr;
}
