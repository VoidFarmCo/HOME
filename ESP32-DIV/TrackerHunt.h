#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * TrackerHunt — find a tracker that is already known to be there.
 *
 * The other two tracker features answer different questions. AirTagSniffer
 * asks "is there an AirTag in range", TrackerFollow asks "has something been
 * beside me for twenty minutes wearing a succession of names". This one is
 * for after the answer is yes: a needle that swings with signal strength so
 * the thing can be walked down to the seat pocket it is in.
 *
 * Two screens. A list of trackers heard in the last minute, then a gauge for
 * the one picked.
 *
 * ── What the needle means, and what it does not ──────────────────────────
 *
 * It is RSSI, smoothed. It is not distance and this screen never claims a
 * number in metres, because the mapping from one to the other needs the
 * transmit power, the antenna orientation and an unobstructed path, and a
 * hunt has none of the three. A tracker under a car seat reads weaker than
 * one twice as far away in open air, and turning round with the board in
 * your hand moves the needle more than walking a metre does.
 *
 * What it is good for is change. Walk, watch which way the needle goes, keep
 * the direction that raises it. That is the whole technique, and it works
 * despite everything in the paragraph above, which is why the screen shows a
 * peak marker -- the best reading of this hunt -- rather than a distance.
 *
 * ── The lock is on an address, and addresses rotate ──────────────────────
 *
 * Picking a row locks onto a BLE address. Find My devices rotate theirs, and
 * a rotation ends the lock: the gauge goes to NO SIGNAL and the device comes
 * back as a new row in the list. Nothing here can follow it across the
 * change -- that is the point of the rotation -- so the practical advice is
 * that a hunt is a few minutes' work, and if the needle dies while you are
 * still holding the thing, go back and re-pick.
 *
 * Receive only. Passive scan, never connects, never asks a device anything.
 * ──────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace TrackerHunt {

/* Trackers we have heard recently. Sized for a screenful and then some; a
 * hunt in a car park does not need a hundred rows, it needs the strong ones,
 * and the list is sorted by RSSI. */
static constexpr int kMaxTargets = 24;

struct Target {
  uint8_t  mac[6];
  int8_t   rssi;          // most recent
  int8_t   best;          // strongest seen since it entered the list
  uint32_t lastSeen;      // millis()
  uint16_t hits;
  char     label[18];     // "AirTag", "Tile", "SmartTag", ...
};

void setup();
void loop();
void exit();

}  // namespace TrackerHunt
