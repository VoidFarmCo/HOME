#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * DroneScan — listening for Broadcast Remote ID.
 *
 * Aircraft required to carry Remote ID announce who and where they are, in
 * the clear, with no pairing and no key, to anybody in range. This listens.
 * It transmits nothing, associates with nothing and connects to nothing,
 * which puts it beside Spotter rather than beside the jammers.
 *
 * Two receivers, both of which this board already had:
 *
 *   Wi-Fi Beacon   promiscuous management frames, walking the information
 *                  elements for the ASD-STAN vendor element. Spotter's
 *                  handler already did this; the walk here is its sibling.
 *
 *   BLE 4 legacy   a passive scan, reading service data under 0xFFFA. The
 *                  same shape FastPairScan reads under 0xFE2C.
 *
 * What it cannot hear is stated on screen rather than left for the user to
 * infer from an empty list: BLE 5 Long Range is the transport a lot of newer
 * aircraft prefer, and a classic ESP32 has no receiver for it. Wi-Fi NAN is
 * a fourth transport and is not implemented. "Nothing found" therefore means
 * "nothing on Wi-Fi Beacon or BLE 4 legacy", which is a narrower claim than
 * "no drones", and the header says so.
 *
 * The decode itself is in DroneId.{h,cpp} and knows nothing about radios or
 * screens, which is what lets tools/check_droneid.py test it without either.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace DroneScan {

void setup();
void loop();
void exit();

}  // namespace DroneScan
