# Battery gauge

The status bar draws a battery outline and an "N%" readout. Both come from
`readBatteryVoltage()`, which averages `analogReadMilliVolts(BATTERY_ADC_PIN)`
and maps 3.00-4.20 V onto 0-100%. That only means something on a board where a
real pin reads the cell.

This board is not one of them. It is the Sunton ESP32-3248S035R, the
`BOARD_CYD` path. The BAT1 cell runs into the FM5324GA charger and reaches no
free ADC pin: GPIO 34 is the CdS light sensor, 35 is the CC1101 RX line, and
36/39 are the XPT2046 touch controller (see `hardware.md`). So `shared.h` sets
`BATTERY_ADC_PIN -1` for `BOARD_CYD`. `readBatteryVoltage()` then reads a dead
pin, and the gauge used to sit pinned at a fake 0% with an empty red battery, a
readout that looks like a flat pack on a device that simply has no gauge.

## What it does now

The outline and the "%" readout in `drawStatusBar()` are wrapped in a
compile-time guard:

```c
#if BATTERY_ADC_PIN >= 0
    /* battery outline, fill and "N%" */
#endif
```

On this board the whole block compiles away, and the left end of the bar is
just empty. That is honest about there being nothing to read. The rest of the
bar (build name, Wi-Fi/BLE/temperature/SD icons) is unchanged.

The guard is keyed to the pin, not the board, on purpose. A board whose overlay
sets a real `BATTERY_ADC_PIN`, whether a wired sense divider or the ESP32-DIV
at GPIO 36, draws the gauge again with no further change, and the DIV builds
stay byte-for-byte what they were before.

## Check

`tools/check_battery_gauge.py` runs 7 checks. It holds that the outline and the
"%" readout both sit inside a `BATTERY_ADC_PIN >= 0` guard, that a bare
`#if BATTERY_ADC_PIN` does not count (because `-1` is truthy and would still
draw the fake gauge), that the guard is present, and that `BOARD_CYD` still
disables the pin so the guard has a reason to exist. Removing the guard, or
weakening it to the bare form, fails the check. Both were confirmed by
mutation.
