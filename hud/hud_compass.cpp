#include "hud_compass.h"
#include "hud_i2c.h"
#include "board_c5.h"
#include <Wire.h>
#include <Arduino.h>
#include <Preferences.h>
#include <math.h>

static bool    s_present = false, s_have = false, s_cal = false;
static int16_t s_x = 0, s_y = 0, s_z = 0;
static int16_t s_ox = 0, s_oy = 0;                 // hard-iron offsets (NVS)
static int16_t s_minx = 32767, s_maxx = -32768, s_miny = 32767, s_maxy = -32768;
static uint32_t s_last = 0;

static void w8(uint8_t reg, uint8_t val) {
  Wire.beginTransmission(C5_COMPASS_ADDR); Wire.write(reg); Wire.write(val); Wire.endTransmission();
}

void hud_compass_begin() {
  hud_i2c_begin();
  Wire.beginTransmission(C5_COMPASS_ADDR);
  s_present = (Wire.endTransmission() == 0);        // chip ACKs?
  if (!s_present) return;
  w8(0x0A, 0x80); delay(10);                        // soft reset
  w8(0x0B, 0x01);                                   // SET/RESET period (datasheet)
  w8(0x09, 0x1D);                                   // continuous, 200 Hz, 8 G, OSR 512
  Preferences p; p.begin("hudcmp", true);
  s_ox = p.getShort("ox", 0); s_oy = p.getShort("oy", 0);
  p.end();
}

bool hud_compass_present() { return s_present; }

void hud_compass_tick(uint32_t now) {
  if (!s_present || now - s_last < 50) return;      // ~20 Hz
  s_last = now;
  Wire.beginTransmission(C5_COMPASS_ADDR); Wire.write(0x00);
  if (Wire.endTransmission(false) != 0) return;
  if (Wire.requestFrom(C5_COMPASS_ADDR, (uint8_t)6) != 6) return;
  uint8_t xl = Wire.read(), xh = Wire.read();       // read into locals: Wire.read() order
  uint8_t yl = Wire.read(), yh = Wire.read();       // in one expression is unsequenced
  uint8_t zl = Wire.read(), zh = Wire.read();
  s_x = (int16_t)(xl | (xh << 8));
  s_y = (int16_t)(yl | (yh << 8));
  s_z = (int16_t)(zl | (zh << 8));
  s_have = true;
  if (s_cal) {
    if (s_x < s_minx) s_minx = s_x; if (s_x > s_maxx) s_maxx = s_x;
    if (s_y < s_miny) s_miny = s_y; if (s_y > s_maxy) s_maxy = s_y;
  }
}

float hud_compass_heading() {
  if (!s_have) return -1.0f;
  float hx = (float)(s_x - s_ox), hy = (float)(s_y - s_oy);
  float h = atan2f(hy, hx) * 57.29578f;
  if (h < 0) h += 360.0f;
  return h;
}

int16_t hud_compass_raw(int axis) { return axis == 0 ? s_x : axis == 1 ? s_y : s_z; }

void hud_compass_start_cal() {
  s_cal = true; s_minx = 32767; s_maxx = -32768; s_miny = 32767; s_maxy = -32768;
}
void hud_compass_end_cal() {
  if (!s_cal) return;
  s_cal = false;
  if (s_maxx > s_minx && s_maxy > s_miny) {         // learned a real spread -> store it
    s_ox = (int16_t)(((int)s_minx + s_maxx) / 2);
    s_oy = (int16_t)(((int)s_miny + s_maxy) / 2);
    Preferences p; p.begin("hudcmp", false);
    p.putShort("ox", s_ox); p.putShort("oy", s_oy);
    p.end();
  }
}
bool hud_compass_calibrating() { return s_cal; }
