#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * DeviceInfo — read the standard Device Information Service from one device.
 *
 * GATT service 0x180A. Every string in it is one the device publishes about
 * itself for exactly this purpose, so reading it is not an attack: it is the
 * service answering the question it exists to answer.
 *
 * What it is for here is firmware versions. An advertisement says what a
 * device is; it does not say which build it is running, and "which build" is
 * the question behind "has this been patched". If a vendor bumps a revision
 * string with a fix, this is where you see it.
 *
 * Deliberately narrow:
 *
 *   One device, chosen by the operator. Nothing here scans, and nothing
 *   sweeps a list. The address is passed in because the caller has to have
 *   picked it.
 *
 *   Reads only. No writes, no pairing, no authentication, and no
 *   characteristic outside 0x180A. If a device wants a bond before it will
 *   answer, the read fails and that is the answer.
 *
 *   Refused in Stealth mode. A GATT connection transmits.
 *
 * The narrowness is the point rather than caution for its own sake. A tool
 * that connects to one device you selected is a diagnostic. The same code
 * pointed at everything in range is something else, and the difference is
 * only ever in the caller.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include <Arduino.h>

namespace DeviceInfo {

enum class Status : uint8_t {
  Ok = 0,        // connected and 0x180A answered
  NoClient,      // could not allocate a client
  ConnectFailed, // device did not accept the connection
  NoService,     // connected, but no 0x180A
  Refused,       // Stealth mode
};

struct Result {
  Status status = Status::NoClient;
  String manufacturer;
  String model;
  String serial;
  String firmware;
  String hardware;
  String software;
  uint8_t found = 0;   // how many of the six had a value
};

/* `addr` is six bytes in written order, AA:BB:CC:DD:EE:FF, the same order
 * the scanner hands out. Blocks for up to the connect timeout. */
void read(const uint8_t addr[6], bool isPublic, Result& out);

/* One line describing a status, for the screen. */
const char* statusText(Status s);

}  // namespace DeviceInfo
