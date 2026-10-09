#include "hud_i2c.h"
#include "board_c5.h"
#include <Wire.h>

static bool s_up = false;
void hud_i2c_begin() {
  if (s_up) return;
  Wire.begin(C5_I2C_SDA, C5_I2C_SCL, C5_I2C_HZ);
  s_up = true;
}
