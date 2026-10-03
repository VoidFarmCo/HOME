#pragma once
/* System > Device Info: a local diagnostics screen -- firmware, board, chip,
 * flash, RAM, SD and uptime. The HaleHound "Device Info" tool, for this device
 * rather than a remote one (DeviceInfo.cpp reads a remote BLE device's info). */
namespace SysInfo {
void setup();
void loop();
}
