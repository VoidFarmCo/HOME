#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * TrackerFollow — spotting a tracker that rotates its identity to avoid being
 * spotted.
 *
 * AirTagSniffer keys its rows on the BLE address. That is fine for a real
 * AirTag, which rotates on a slow schedule, and useless against a clone: the
 * published ESP32 clone cycles a couple of thousand public keys, so every
 * rotation produces a fresh row, `hits` resets to 1, and after 32 identities
 * the list starts evicting. The harder something works to avoid detection,
 * the less it stands out. That is the wrong way round.
 *
 * The fix is to stop asking "have I seen THIS DEVICE before" and start asking
 * "has SOMETHING been beside me, at the same distance, wearing a succession
 * of names".
 *
 * ── What this detects ──────────────────────────────────────────────────────
 *
 * One thing, narrowly: **identity rotation from a stable distance**.
 *
 * Sightings are bucketed into RSSI lanes. A lane that stays occupied without
 * a long gap, and collects many distinct identities while it does, is a
 * device keeping station with you and changing its name as it goes. That is
 * the clone's signature and it is not a thing ordinary hardware does.
 *
 * ── What this does NOT detect, and why ─────────────────────────────────────
 *
 * **A plain AirTag following you.** It rotates once in a long while, so it
 * produces a continuous lane with one or two identities, which this scores as
 * nothing. That case is already visible: AirTagSniffer shows it as one row
 * with a rising hit count, which is exactly what a persistent row is for.
 * Apple's own detection covers it too.
 *
 * **Anything, while you are moving through a crowded place.** Lanes fragment,
 * identities churn, and every signal here degrades together. The caller is
 * expected to say so on screen rather than let the numbers imply confidence
 * they do not have.
 *
 * **Which device it is.** There is no key, no address, nothing to hand to
 * anybody. The output is "something is doing this, at about this signal
 * strength". That is the honest limit of one radio with no history.
 *
 * This is a heuristic. Apple does the equivalent server-side with the whole
 * network in view. Scored like Spotter: compute it, show it, claim nothing
 * the signal does not support.
 *
 * No BLE, no display, no allocation, and no clock of its own -- the caller
 * passes the time in. That is what lets tools/check_tracker_follow.py run it
 * on a host against scripted scenarios.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>
#include <stdint.h>

namespace TrackerFollow {

/* How wide an RSSI lane is, in dB. Wide enough to absorb ordinary noise,
 * narrow enough that two devices at different distances do not share one. */
static constexpr int8_t  kLaneWidthDb    = 6;
static constexpr uint8_t kMaxLanes       = 8;

/* A gap longer than this breaks a lane's continuity and restarts it. A
 * follower is not silent for twenty seconds; a passer-by is. */
static constexpr uint32_t kGapToleranceMs = 20000;

/* A lane untouched for this long is recycled. */
static constexpr uint32_t kLaneIdleMs     = 120000;

/* Identities are "distinct" within this window. Seeing the same address
 * again after it expires counts as new, which is the safe direction: it
 * can only inflate churn, and the thresholds below are set well clear. */
static constexpr uint32_t kIdentityMs     = 300000;
static constexpr uint8_t  kSeenSlots      = 64;

/* An identity only counts toward a lane once it has been seen this many
 * times. This is the whole difference between a tail and a crowd, and it
 * was not in the first version.
 *
 * A lane is not "one device holding station", it is "anything at roughly
 * this distance". In a busy place every lane stays occupied by a parade of
 * strangers, so runs accumulate and identity counts climb, and the first
 * cut of this happily reported a shopping centre as a tail. What separates
 * them is that a follower's identities each PERSIST -- a clone key is seen
 * dozens of times before it rotates -- while a passer-by is seen once and
 * is gone. Counting only identities that stayed a while drops the crowd out
 * and leaves the tail. */
static constexpr uint8_t  kIdentityMinSightings = 3;

/* A qualifying lane must average at least one new persistent identity per
 * this many milliseconds.
 *
 * A count alone was not enough, and the desk scenario showed it: half an
 * hour beside your own AirTag reaches two identities, and forty-five
 * minutes reaches three, which was the old Weak threshold. Sitting still
 * would eventually have reported itself as a tail.
 *
 * Rate separates them properly, because it does not accumulate. Apple
 * rotates roughly every quarter of an hour, which is 0.07 identities a
 * minute; the published clone cycles every thirty seconds, which is 2 a
 * minute -- thirty times apart. Four minutes per identity sits between
 * them with room on both sides, and unlike a count it does not creep up
 * just because you stayed somewhere a long time. */
static constexpr uint32_t kMsPerIdentity  = 240000;

/* Sliding window for the churn figure. */
static constexpr uint32_t kChurnWindowMs  = 60000;
static constexpr uint8_t  kChurnSlots     = 48;

enum class Verdict : uint8_t {
  None = 0,
  Weak,     /* >= 3 identities in a lane held >= 2 minutes */
  Likely,   /* >= 6 identities in a lane held >= 5 minutes */
  Strong,   /* >= 12 identities in a lane held >= 10 minutes */
};

struct Lane {
  bool     used;
  int8_t   centre;        /* RSSI, drifts with a slow average */
  uint32_t firstMs;       /* start of the current unbroken run */
  uint32_t lastMs;
  uint16_t identities;    /* distinct addresses seen during that run */
  uint16_t sightings;
};

struct Seen {
  uint32_t hash;
  uint32_t ms;
  uint8_t  count;      /* sightings inside the identity window */
  bool     credited;   /* already counted toward a lane */
};

struct State {
  Lane     lanes[kMaxLanes];
  Seen     seen[kSeenSlots];
  uint8_t  seenNext;
  uint32_t churn[kChurnSlots];   /* ms of each new-identity event */
  uint8_t  churnNext;
  uint32_t newIdentities;        /* lifetime, for the log */
  uint32_t sightings;
};

/** Clear everything. Call when the feature starts. */
void reset(State* st);

/**
 * Record one offline-finding sighting.
 *
 * `macHash` identifies the advertiser; any stable hash of the address will
 * do, and hashing rather than storing it keeps this free of anything that
 * could be mistaken for a record of who was nearby.
 */
void sighting(State* st, uint32_t ms, int8_t rssi, uint32_t macHash);

/** Age out stale lanes. Safe to call often; cheap. */
void tick(State* st, uint32_t ms);

/** The current reading, and the lane driving it (null when None). */
Verdict verdict(const State* st, uint32_t ms);
const Lane* leadLane(const State* st, uint32_t ms);

/** Distinct identities first seen in the last kChurnWindowMs. */
uint16_t churn(const State* st, uint32_t ms);

const char* verdictText(Verdict v);

}  // namespace TrackerFollow
