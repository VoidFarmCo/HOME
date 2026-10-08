#include "hud_gps.h"
#include <Arduino.h>
#include <string.h>
#include <stdlib.h>

static GpsFix s_fix = { false, 0, 0, 0, 0, 0, 0 };
static char   s_line[96];
static int    s_len = 0;
static uint32_t s_rx = 0;   // total UART bytes (is the module talking at all?)

// ddmm.mmmm + hemisphere -> decimal degrees
static double nmea_deg(const char* v, char hemi) {
  if (!v || !*v) return 0;
  double raw = atof(v);
  int deg = (int)(raw / 100);
  double min = raw - deg * 100;
  double d = deg + min / 60.0;
  if (hemi == 'S' || hemi == 'W') d = -d;
  return d;
}

// Parse one NMEA sentence (GGA for fix/sats/alt, RMC for position/course).
static void parse(const char* line) {
  if (strncmp(line, "$G", 2) != 0) return;
  char buf[96];
  strncpy(buf, line, sizeof(buf) - 1);
  buf[sizeof(buf) - 1] = 0;
  char* f[20]; int nf = 0;
  f[nf++] = buf;
  for (char* p = buf; *p && nf < 20; p++) if (*p == ',') { *p = 0; f[nf++] = p + 1; }

  if (strstr(f[0], "GGA") && nf >= 10) {
    int q = atoi(f[6]);
    s_fix.sats = atoi(f[7]);
    s_fix.hdop = atof(f[8]);
    s_fix.altm = atof(f[9]);
    if (q > 0 && f[2][0]) {
      s_fix.lat = nmea_deg(f[2], f[3][0]);
      s_fix.lon = nmea_deg(f[4], f[5][0]);
      s_fix.valid = true;
    } else {
      s_fix.valid = false;
    }
  } else if (strstr(f[0], "RMC") && nf >= 9) {
    if (f[2][0] == 'A') {
      s_fix.lat = nmea_deg(f[3], f[4][0]);
      s_fix.lon = nmea_deg(f[5], f[6][0]);
      s_fix.course = atof(f[8]);
      s_fix.valid = true;
    }
  }
}

void hud_gps_inject(const char* line) { parse(line); }

void hud_gps_begin() {
  // NEO-7M @ 9600 8N1. RX = IO4 (on the P1 JST; wire GPS-TX here). TX = IO28, a free
  // pin left UNWIRED (the module needs no commands) -- keeps IO8/IO9 free for I2C.
  Serial1.begin(9600, SERIAL_8N1, 4, 28);
}

void hud_gps_tick() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    s_rx++;
    if (c == '\n' || c == '\r') {
      if (s_len) { s_line[s_len] = 0; parse(s_line); s_len = 0; }
    } else if (s_len < (int)sizeof(s_line) - 1) {
      s_line[s_len++] = c;
    }
  }
}

const GpsFix& hud_gps() { return s_fix; }
uint32_t      hud_gps_rxbytes() { return s_rx; }
