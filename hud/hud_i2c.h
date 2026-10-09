#pragma once
// Shared I2C bus bring-up (SDA 9 / SCL 8 on the CN1 header). The compass and -- later --
// the MCP23017 expander both ride it. Idempotent: safe to call from each module's begin().
void hud_i2c_begin();
