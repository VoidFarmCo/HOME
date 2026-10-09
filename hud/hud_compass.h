#pragma once
#include <stdint.h>
// QMC5883L 3-axis magnetometer on the I2C header (addr 0x0D). Gives a tilt-naive
// heading (0..360, 0 = N) for the moving map and the RADAR direction-finder. Hard-iron
// offsets are learned by a spin calibration and kept in NVS. Returns -1 until it reads.
void    hud_compass_begin();                 // after I2C is up
bool    hud_compass_present();               // did the chip ACK on the bus?
void    hud_compass_tick(uint32_t now);      // ~20 Hz read (non-blocking cadence)
float   hud_compass_heading();               // degrees 0..360, or -1 if no reading yet
int16_t hud_compass_raw(int axis);           // 0=x 1=y 2=z (debug)
void    hud_compass_start_cal();             // begin a spin calibration (collect min/max)
void    hud_compass_end_cal();               // finish: store hard-iron offsets to NVS
bool    hud_compass_calibrating();
