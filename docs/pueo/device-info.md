# Device Info (System)

**System, Device Info.** A local diagnostics screen for this device, the way
HaleHound's Tools has one. (The existing `DeviceInfo.cpp` is unrelated: it reads
a *remote* BLE device's Device Information Service. This is ours.)

Shows, drawn once:
- firmware name + version (H.O.M.E 0.4.27)
- board name (`ESP32DIV_BOARD_NAME`)
- chip model, cores, clock
- flash size, PSRAM (or "none")
- chip ID (eFuse)
- SD present / none

and, refreshed every second:
- free / total heap
- uptime

Press **Exit** to return to the System menu.

## Wiring

A small `SysInfo` namespace (`SysInfo.cpp` / `.h`) with `setup()` + `loop()`,
run by `runToolsFeature()` like the other System tools. It is row
`TOOLS_IDX_DEVINFO` (7) in `tools_submenu_items`, which pushed Back to 8; the
dispatch is a `case` in `launchToolsFeature()`. The menu-integrity checks
(`check_menu_dispatch`, `check_menu_tables`, `check_grid_capacity`) cover the
wiring, so this feature adds no check of its own -- the screen is read-only and
has no behavior of its own to pin.

Radio Test (probe each module + wiring) is the other HaleHound tool H.O.M.E does
not have yet; it is queued to build alongside the RF hat.
