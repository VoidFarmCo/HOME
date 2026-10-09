#pragma once
#include <stdint.h>

// NEO-7M (NMEA) GPS. The module is NOT wired yet; this parses $..GGA/$..RMC when
// it is (UART RX 4 / TX 5 @ 9600). hud_gps_inject() feeds one sentence for testing
// without the hardware. Until there's a fix, valid stays false.
struct GpsFix {
  bool   valid;      // has a position fix
  double lat, lon;   // decimal degrees
  int    sats;       // satellites used
  float  hdop;       // horizontal dilution
  float  altm;       // altitude (m)
  float  course;     // heading over ground (deg), from RMC
  float  knots;      // speed over ground (knots), from RMC
  char   utc[7];     // UTC time hhmmss (empty until parsed) -- for log timestamps
  char   date[7];    // UTC date ddmmyy (empty until parsed)
};

void          hud_gps_begin();              // open the UART
void          hud_gps_tick();               // drain the UART, parse (non-blocking)
const GpsFix& hud_gps();                     // latest fix
void          hud_gps_inject(const char* nmea_line);  // feed one sentence (test/replay)
uint32_t      hud_gps_rxbytes();            // total bytes seen on the UART (wiring check)
