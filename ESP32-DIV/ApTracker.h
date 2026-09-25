#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * ApTracker — walk down a Wi-Fi access point you have already found.
 *
 * WiFi Scanner answers "what is in range". This is for after that answer: a
 * needle that swings with signal strength so a particular AP can be walked
 * to the cupboard it is in.
 *
 * ── Why this is not a graph on the scanner ───────────────────────────────
 *
 * The obvious version of this feature is a scrolling RSSI history on the
 * scanner's list. It does not work, for a reason worth writing down.
 *
 * WiFi.scanNetworks() is a sweep, not a stream. Fourteen channels at
 * wifiStaScanMsPerChannel(), which floors at 120 ms, is 1.7 seconds per
 * sample at best. A graph at 0.6 Hz implies a resolution it does not have,
 * and you would take three or four steps between points.
 *
 * It also transmits. An active scan puts a probe request carrying this
 * device's address on every channel, so a feature that rescans forever to
 * feed a graph announces you forever.
 *
 * Parking on one channel and listening to one AP's beacons samples about ten
 * times a second, because the default beacon interval is 100 TU or 102.4 ms,
 * and transmits nothing at all. Sixteen times the rate, none of the noise.
 *
 * ── What the needle means ────────────────────────────────────────────────
 *
 * The same thing it means in Hunt, and the same caveats: it is RSSI, it is
 * not distance, and the technique is to move and watch which way it goes.
 * SignalGauge draws it and its header says why there is no number in metres.
 *
 * ── What will lose the lock ──────────────────────────────────────────────
 *
 * Band steering or a DFS event moves the AP to another channel and the
 * beacons stop arriving, which looks exactly like walking out of range. The
 * gauge says LOST either way and the advice is the same: go back and re-pick.
 *
 * ── 2.4 GHz only ─────────────────────────────────────────────────────────
 *
 * The ESP32's radio is 2.4 GHz, so a 5 GHz AP is not in the picker and never
 * will be. The picker says so rather than leaving someone hunting for a
 * network they can see on a phone.
 *
 * Receive only. The one scan that populates the picker is the scanner's own;
 * after that this feature never transmits.
 * ──────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace ApTracker {

/* A screenful and then some. A picker in a block of flats does not need
 * every SSID, it needs the strong ones, and the list is sorted by RSSI. */
static constexpr int kMaxAps = 24;

void setup();
void loop();
void exit();

}  // namespace ApTracker
