#include "hud_log.h"
#include "hud_engage.h"
#include "hud_gps.h"
#include "hud_scan.h"
#include "hud_comms.h"
#include "hud_sd.h"
#include <Arduino.h>
#include <string.h>
#include <stdio.h>

static bool     s_enabled = false;     // default OFF: the user turns logging on
static uint32_t s_rows = 0;

void     hud_log_set_enabled(bool en) { s_enabled = en; }
bool     hud_log_enabled() { return s_enabled; }
uint32_t hud_log_rows() { return s_rows; }

// ISO-ish UTC from the GPS when available, else uptime.
static void stamp(char* out, int n) {
  const GpsFix& g = hud_gps();
  if (g.date[0] && g.utc[0])
    snprintf(out, n, "20%c%c-%c%c-%c%cT%c%c:%c%c:%c%cZ",
             g.date[4], g.date[5], g.date[2], g.date[3], g.date[0], g.date[1],
             g.utc[0], g.utc[1], g.utc[2], g.utc[3], g.utc[4], g.utc[5]);
  else snprintf(out, n, "up%lus", (unsigned long)(millis() / 1000));
}
static void wln(const char* file, const char* line) { if (hud_sd_append(file, line)) s_rows++; }

// --- threats.csv: each new drone (by id) once + deauth bursts ---
static char     s_drLogged[8][21]; static int s_drLoggedN = 0;
static uint32_t s_lastDeauth = 0, s_deauthT = 0;
static void log_threats(uint32_t now) {
  char ts[32]; stamp(ts, sizeof(ts)); char l[160];
  uint32_t dc = hud_engage_deauth();
  if (dc != s_lastDeauth && now - s_deauthT > 2000) {
    s_lastDeauth = dc; s_deauthT = now;
    snprintf(l, sizeof(l), "%s,DEAUTH,count=%lu,ch=%d", ts, (unsigned long)dc, hud_engage_channel());
    wln("threats.csv", l);
  }
  int dn = hud_engage_drone_count(); DroneInfo d;
  for (int i = 0; i < dn; i++) {
    if (!hud_engage_drone(i, &d) || !d.id[0]) continue;
    bool known = false;
    for (int j = 0; j < s_drLoggedN; j++) if (!strcmp(s_drLogged[j], d.id)) { known = true; break; }
    if (known) continue;
    if (s_drLoggedN < 8) { strncpy(s_drLogged[s_drLoggedN], d.id, 20); s_drLogged[s_drLoggedN][20] = 0; s_drLoggedN++; }
    snprintf(l, sizeof(l), "%s,DRONE,%s,%.6f,%.6f,op,%.6f,%.6f,rssi=%d", ts, d.id,
             d.loc ? d.lat : 0.0, d.loc ? d.lon : 0.0, d.op ? d.oplat : 0.0, d.op ? d.oplon : 0.0, d.rssi);
    wln("threats.csv", l);
  }
}

// --- track.csv: a GPS breadcrumb every TRACK_MS ---
#define TRACK_MS 5000
static void log_track(uint32_t now) {
  static uint32_t s_t = 0;
  const GpsFix& g = hud_gps();
  if (!g.valid || now - s_t < TRACK_MS) return;
  s_t = now;
  char ts[32]; stamp(ts, sizeof(ts)); char l[96];
  snprintf(l, sizeof(l), "%s,%.6f,%.6f,%dm,%dsat,%.1fkn,%03d", ts, g.lat, g.lon, (int)g.altm, g.sats, g.knots, (int)g.course);
  wln("track.csv", l);
}

// --- comms.csv: each chat message / mark as it happens ---
static uint32_t s_lastTotal = 0;
static void log_comms(uint32_t) {
  uint32_t tot = hud_comms_total();
  if (tot <= s_lastTotal) return;
  s_lastTotal = tot;
  int n = hud_comms_count(); if (n <= 0) return;
  const ChatMsg& m = hud_comms_log()[n - 1];         // newest
  char ts[32]; stamp(ts, sizeof(ts)); char l[96];
  snprintf(l, sizeof(l), "%s,%s,%s,%s", ts, m.me ? "TX" : "RX", m.from, m.text);
  wln("comms.csv", l);
}

// --- wifi.csv: the TRACKED AP only -- log appear/disappear transitions ---
static bool s_wifiTracked = false, s_wifiSeen = false;
static void log_wifi(uint32_t) {
  if (!hud_scan_has_target()) { s_wifiTracked = false; return; }
  bool seen = hud_scan_target_seen();
  if (!s_wifiTracked || seen != s_wifiSeen) {
    s_wifiTracked = true; s_wifiSeen = seen;
    char ts[32]; stamp(ts, sizeof(ts)); char l[96];
    snprintf(l, sizeof(l), "%s,%s,%s,rssi=%d", ts, hud_scan_target_name(), seen ? "SEEN" : "LOST", hud_scan_target_rssi());
    wln("wifi.csv", l);
  }
}

void hud_log_tick(uint32_t now) {
  if (!s_enabled || !hud_sd_ok()) return;
  log_threats(now);
  log_track(now);
  log_comms(now);
  log_wifi(now);
}
