#!/usr/bin/env python3
"""QMC5883L compass driver (I2C) + spin calibration.

Brings up I2C (SDA9/SCL8), ACK-probes the chip at 0x0D, puts it in continuous mode,
reads X/Y/Z into SEPARATE locals (an inline multi-Wire.read() expression is unsequenced
-> swapped bytes), computes a heading, and learns hard-iron offsets stored in NVS.
Wired into setup + the loop tick. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
C = (H / "hud_compass.cpp").read_text(encoding="utf-8", errors="replace")
BD = (H / "board_c5.h").read_text(encoding="utf-8", errors="replace")
I2 = (H / "hud_i2c.cpp").read_text(encoding="utf-8", errors="replace")
INO = (H / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("I2C bus on SDA9/SCL8; compass addr 0x0D",
       "Wire.begin(C5_I2C_SDA, C5_I2C_SCL" in I2 and re.search(r"#define\s+C5_I2C_SDA\s+9", BD) is not None
       and re.search(r"#define\s+C5_I2C_SCL\s+8", BD) is not None
       and re.search(r"#define\s+C5_COMPASS_ADDR\s+0x0D", BD) is not None)
    ok("present() = chip ACKs; continuous mode configured",
       "endTransmission() == 0" in C and "w8(0x09, 0x1D)" in C)
    ok("X/Y/Z read into separate locals (no unsequenced Wire.read())",
       re.search(r"uint8_t xl = Wire\.read\(\), xh = Wire\.read\(\);", C) is not None
       and "s_x = (int16_t)(xl | (xh << 8))" in C)
    ok("heading via atan2, normalised 0..360",
       "atan2f(hy, hx)" in C and "h += 360" in C)
    ok("hard-iron spin calibration stored in NVS",
       "hud_compass_start_cal" in C and "hud_compass_end_cal" in C and 'p.putShort("ox"' in C)
    ok("wired into setup + loop",
       "hud_compass_begin()" in INO and "hud_compass_tick(millis())" in INO)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
