#include "hud_dronelog.h"
#include "hud_engage.h"
#include "hud_gps.h"
#include "hud_sd.h"
#include <Arduino.h>
#include <stdio.h>
#include <string.h>

#define LOG_FILE     "drones.csv"
#define LOG_INTERVAL 10000        // ms between log snapshots

static uint32_t s_last = 0;
static bool     s_hdr  = false;
static uint32_t s_rows = 0;

// ISO-ish UTC from the GPS date+time when we have them, else boot uptime.
static void stamp(char* out, int n) {
  const GpsFix& g = hud_gps();
  if (g.date[0] && g.utc[0])
    snprintf(out, n, "20%c%c-%c%c-%c%cT%c%c:%c%c:%c%cZ",
             g.date[4], g.date[5], g.date[2], g.date[3], g.date[0], g.date[1],
             g.utc[0], g.utc[1], g.utc[2], g.utc[3], g.utc[4], g.utc[5]);
  else
    snprintf(out, n, "up%lus", (unsigned long)(millis() / 1000));
}

void hud_dronelog_tick(uint32_t now) {
  if (!hud_sd_ok()) return;                 // no card / not FAT32 -> nothing to do
  int dn = hud_engage_drone_count();
  if (dn <= 0) return;                      // only log when a drone is actually present
  if (now - s_last < LOG_INTERVAL) return;
  s_last = now;
  if (!s_hdr) {                             // session header (once per boot)
    hud_sd_append(LOG_FILE, "# HUD drone log -- new session");
    hud_sd_append(LOG_FILE, "time,id,dlat,dlon,oplat,oplon,rssi,mylat,mylon");
    s_hdr = true;
  }
  const GpsFix& g = hud_gps();
  char ts[32]; stamp(ts, sizeof(ts));
  DroneInfo d;
  for (int i = 0; i < dn; i++) {
    if (!hud_engage_drone(i, &d)) continue;
    char row[176];
    snprintf(row, sizeof(row), "%s,%s,%.6f,%.6f,%.6f,%.6f,%d,%.6f,%.6f",
             ts, d.id[0] ? d.id : "?",
             d.loc ? d.lat : 0.0, d.loc ? d.lon : 0.0,
             d.op ? d.oplat : 0.0, d.op ? d.oplon : 0.0,
             d.rssi, g.valid ? g.lat : 0.0, g.valid ? g.lon : 0.0);
    if (hud_sd_append(LOG_FILE, row)) s_rows++;
  }
}

uint32_t hud_dronelog_rows() { return s_rows; }
