#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * SignalGauge — a needle that swings with signal strength.
 *
 * Extracted from TrackerHunt, which built it to walk a BLE tracker down to
 * the seat pocket it is in. The technique is not specific to Bluetooth: any
 * radio you can sample repeatedly can be walked down the same way, and the
 * AP tracker wants exactly this screen against Wi-Fi beacons.
 *
 * ── What the needle means, and what it does not ──────────────────────────
 *
 * It is RSSI, smoothed. It is never distance, and this screen never prints a
 * number in metres, because the mapping from one to the other needs the
 * transmit power, the antenna orientation and an unobstructed path, and a
 * hunt has none of the three. A tracker under a car seat reads weaker than
 * one twice as far away in open air, and turning round with the board in
 * your hand moves the needle more than walking a metre does.
 *
 * What it is good for is change. Walk, watch which way the needle goes, keep
 * the direction that raises it. That is why the direction is the biggest
 * thing on the screen, the number is the smallest, and there is a peak
 * marker rather than a distance.
 *
 * ── Ownership ────────────────────────────────────────────────────────────
 *
 * The gauge owns the smoothing, the peak, the trend and the partial redraw
 * cache, because all four are properties of watching a signal rather than of
 * whatever is being watched. The caller owns the target: it decides what is
 * locked, when a sample arrives, when the lock is lost, and what the two
 * header strings say.
 * ──────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace SignalGauge {

/* The ends of the dial. -100 is the floor of anything worth showing and -35
 * is close enough that the reading stops meaning much. */
constexpr int kRssiFar  = -100;
constexpr int kRssiNear = -35;

/* Start a new lock. Clears the smoothing, the peak, the trend and the
 * redraw cache, so the first frame after this draws everything. */
void reset();

/* One reading. Smoothing and the peak happen here, so a caller that hears
 * its target ten times a second and one that hears it twice both get a
 * needle that moves at a readable rate. */
void sample(int8_t rssi);

/* Draw. `title` goes top left, `sub` top right (an address, usually), and
 * `hits` is the sighting count. `lost` parks the needle and swaps the
 * readout for an explanation; the caller decides what lost means, since a
 * BLE address rotating and an AP changing channel look the same from here.
 *
 * `hint1` and `hint2` are the two lines under LOST, so each caller can say
 * the thing that is actually likely for it: a BLE tracker has rotated its
 * address, an AP has changed channel or you have walked out of range. Two
 * lines because one does not hold the sentence at this width. */
void draw(const char* title, const char* sub, uint32_t hits,
          bool lost, const char* hint1, const char* hint2);

/* Drop the peak marker back to a given reading, without disturbing the
 * smoothing or the lock. This is the "Reset" button on the gauge: the peak
 * is the best of this hunt, and once you have walked past something and
 * back the old high water mark stops being useful. */
void resetPeak(int8_t to);

/* The smoothed value and the best seen, for a caller that wants to log or
 * display them outside the gauge. */
float smoothed();
int8_t peak();

}  // namespace SignalGauge
