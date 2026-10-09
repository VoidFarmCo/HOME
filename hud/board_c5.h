#pragma once
// NM-CYD-C5 module/pin map -- the ONE source of truth for the combat radio+sensor stack.
// Cross-checked against the firmware's live pins and D:\Projects\CombatHUD\refs\HARDWARE-PINS.md.
//
// IN USE (do not reuse):
//   SPI2 bus  SCLK 6  MOSI 7  MISO 2     (display + touch + SD share it)
//   CS        display 23, touch 1, SD 10
//   display   DC 24, RST -1, BL 25
//   GPS       RX 4 (GPS-TX -> here), TX 28 (unwired; reusable)
//
// FREE for this stack:  direct pins IO0 + IO28 (GPS-TX, unwired) ; I2C header SDA 9 / SCL 8
//
// ---- I2C (shared: compass + MCP23017 expander) ----
#define C5_I2C_SDA        9
#define C5_I2C_SCL        8
#define C5_I2C_HZ         400000

// ---- QMC5883L magnetometer / compass (Phase 1) ----
#define C5_COMPASS_ADDR   0x0D      // QMC5883L (HMC5883L would be 0x1E)

// ---- MCP23017 expander + radios (Phase 2/3; here for reference) ----
// MCP23017 addr 0x20 on the I2C header. Its channels carry the SLOW radio control lines;
// the two timing-critical lines stay on direct GPIO.
#define C5_MCP_ADDR       0x20
#define C5_PIN_BASE       100       // pins >= base route through the MCP23017 (Mcp23017 shim)
// direct, timing-critical:
#define C5_CC1101_GDO0    0         // IO0  -- ELECHOUSE lib polls this via digitalRead
#define C5_LORA_BUSY      28        // IO28 -- SX1262 BUSY, polled fast
// on the expander (C5_PIN_BASE + channel):
#define C5_CC1101_CS      (C5_PIN_BASE + 0)
#define C5_LORA_CS        (C5_PIN_BASE + 1)
#define C5_LORA_RESET     (C5_PIN_BASE + 2)
#define C5_LORA_TXEN      (C5_PIN_BASE + 3)   // RF-switch enable (if the LoRa board needs it)
