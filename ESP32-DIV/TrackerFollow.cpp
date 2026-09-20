#include "TrackerFollow.h"

#include <string.h>

namespace TrackerFollow {

namespace {

/* Thresholds. Each grade needs BOTH a run length and an identity count,
 * because either alone is ordinary:
 *
 *   A long run with one or two identities is a device sitting still near
 *   another device sitting still. That is a desk, not a tail.
 *
 *   Many identities with no run is a crowd. Every phone taking part in Find
 *   My emits the short form of this message, so a concourse produces
 *   identities endlessly and no continuity at all.
 *
 * It is the pair that is unusual: something holding station while changing
 * its name. A real AirTag rotates slowly enough that twenty minutes beside
 * one yields one or two identities, which lands under every grade here. */
struct Grade {
  uint32_t holdMs;
  uint16_t identities;
  Verdict  verdict;
};
const Grade kGrades[] = {
  { 600000, 12, Verdict::Strong },
  { 300000,  6, Verdict::Likely },
  { 120000,  3, Verdict::Weak   },
};
constexpr size_t kGradeCount = sizeof(kGrades) / sizeof(kGrades[0]);

int absDiff(int a, int b) {
  return (a > b) ? (a - b) : (b - a);
}

/* Did this lane collect its identities fast enough to be a rotating device
 * rather than a long sit? Integer form of
 *     identities / minutes >= 1 / (kMsPerIdentity / 60000)
 * with no division and no float. */
bool qualifiesByRate(uint32_t heldMs, uint16_t identities) {
  if (identities == 0) {
    return false;
  }
  return (uint64_t)identities * kMsPerIdentity >= (uint64_t)heldMs;
}

/* What one sighting of an address means. */
struct Look {
  bool isNew;     /* not seen inside the identity window */
  bool matured;   /* just crossed kIdentityMinSightings; counts once */
};

/* Record a sighting of `hash` and say what it was. */
Look lookUp(State* st, uint32_t ms, uint32_t hash) {
  Look r{false, false};
  for (uint8_t i = 0; i < kSeenSlots; i++) {
    if (st->seen[i].count != 0 && st->seen[i].hash == hash) {
      Seen& e = st->seen[i];
      if ((uint32_t)(ms - e.ms) > kIdentityMs) {
        e.ms = ms;                // window expired: this is a new identity
        e.count = 1;
        e.credited = false;
        r.isNew = true;
        return r;
      }
      e.ms = ms;
      if (e.count < 0xFF) {
        e.count++;
      }
      if (!e.credited && e.count >= kIdentityMinSightings) {
        e.credited = true;
        r.matured = true;
      }
      return r;
    }
  }
  Seen& e = st->seen[st->seenNext];
  e.hash = hash;
  e.ms = ms;
  e.count = 1;
  e.credited = false;
  st->seenNext = (uint8_t)((st->seenNext + 1) % kSeenSlots);
  r.isNew = true;
  return r;
}

void noteChurn(State* st, uint32_t ms) {
  st->churn[st->churnNext] = ms;
  st->churnNext = (uint8_t)((st->churnNext + 1) % kChurnSlots);
  if (st->newIdentities < 0xFFFFFFFFUL) {
    st->newIdentities++;
  }
}

/* Nearest lane within tolerance, else a free one, else the stalest. */
int pickLane(State* st, uint32_t ms, int8_t rssi) {
  int best = -1;
  int bestDelta = kLaneWidthDb + 1;
  for (uint8_t i = 0; i < kMaxLanes; i++) {
    if (!st->lanes[i].used) {
      continue;
    }
    const int d = absDiff(rssi, st->lanes[i].centre);
    if (d <= kLaneWidthDb && d < bestDelta) {
      bestDelta = d;
      best = (int)i;
    }
  }
  if (best >= 0) {
    return best;
  }
  for (uint8_t i = 0; i < kMaxLanes; i++) {
    if (!st->lanes[i].used) {
      st->lanes[i] = Lane{};
      st->lanes[i].used = true;
      st->lanes[i].centre = rssi;
      st->lanes[i].firstMs = ms;
      st->lanes[i].lastMs = ms;
      return (int)i;
    }
  }
  int stale = 0;
  for (uint8_t i = 1; i < kMaxLanes; i++) {
    if ((int32_t)(st->lanes[i].lastMs - st->lanes[stale].lastMs) < 0) {
      stale = (int)i;
    }
  }
  st->lanes[stale] = Lane{};
  st->lanes[stale].used = true;
  st->lanes[stale].centre = rssi;
  st->lanes[stale].firstMs = ms;
  st->lanes[stale].lastMs = ms;
  return stale;
}

}  // namespace

void reset(State* st) {
  if (st == nullptr) {
    return;
  }
  memset(st, 0, sizeof(*st));
}

void sighting(State* st, uint32_t ms, int8_t rssi, uint32_t macHash) {
  if (st == nullptr) {
    return;
  }
  if (st->sightings < 0xFFFFFFFFUL) {
    st->sightings++;
  }

  const Look look = lookUp(st, ms, macHash);
  if (look.isNew) {
    noteChurn(st, ms);
  }

  const int idx = pickLane(st, ms, rssi);
  Lane& L = st->lanes[idx];

  /* A gap means whatever was here left and something else arrived. Start the
   * run again rather than crediting the new arrival with the old one's
   * dwell. */
  if ((uint32_t)(ms - L.lastMs) > kGapToleranceMs) {
    L.firstMs = ms;
    L.identities = 0;
    L.sightings = 0;
  }

  /* Slow average, so the lane follows a device whose distance drifts without
   * chasing a single noisy reading. */
  L.centre = (int8_t)((L.centre * 3 + rssi) / 4);
  L.lastMs = ms;
  if (L.sightings < 0xFFFF) {
    L.sightings++;
  }
  /* Only identities that stayed a while count. A passer-by seen once is
   * not evidence of anything; a clone key seen a dozen times before it
   * rotates is. */
  if (look.matured && L.identities < 0xFFFF) {
    L.identities++;
  }
}

void tick(State* st, uint32_t ms) {
  if (st == nullptr) {
    return;
  }
  for (uint8_t i = 0; i < kMaxLanes; i++) {
    if (st->lanes[i].used &&
        (uint32_t)(ms - st->lanes[i].lastMs) > kLaneIdleMs) {
      st->lanes[i] = Lane{};
    }
  }
}

const Lane* leadLane(const State* st, uint32_t ms) {
  if (st == nullptr) {
    return nullptr;
  }
  const Lane* best = nullptr;
  Verdict bestV = Verdict::None;
  for (uint8_t i = 0; i < kMaxLanes; i++) {
    const Lane& L = st->lanes[i];
    if (!L.used) {
      continue;
    }
    /* A run that has gone quiet is not a current finding. */
    if ((uint32_t)(ms - L.lastMs) > kGapToleranceMs) {
      continue;
    }
    const uint32_t held = (uint32_t)(L.lastMs - L.firstMs);
    for (size_t g = 0; g < kGradeCount; g++) {
      if (held >= kGrades[g].holdMs && L.identities >= kGrades[g].identities &&
          qualifiesByRate(held, L.identities)) {
        if (best == nullptr || kGrades[g].verdict > bestV) {
          best = &L;
          bestV = kGrades[g].verdict;
        }
        break;
      }
    }
  }
  return best;
}

Verdict verdict(const State* st, uint32_t ms) {
  const Lane* L = leadLane(st, ms);
  if (L == nullptr) {
    return Verdict::None;
  }
  const uint32_t held = (uint32_t)(L->lastMs - L->firstMs);
  for (size_t g = 0; g < kGradeCount; g++) {
    if (held >= kGrades[g].holdMs && L->identities >= kGrades[g].identities &&
        qualifiesByRate(held, L->identities)) {
      return kGrades[g].verdict;
    }
  }
  return Verdict::None;
}

uint16_t churn(const State* st, uint32_t ms) {
  if (st == nullptr) {
    return 0;
  }
  uint16_t n = 0;
  for (uint8_t i = 0; i < kChurnSlots; i++) {
    if (st->churn[i] != 0 &&
        (uint32_t)(ms - st->churn[i]) <= kChurnWindowMs) {
      n++;
    }
  }
  return n;
}

const char* verdictText(Verdict v) {
  switch (v) {
    case Verdict::None:   return "nothing following";
    case Verdict::Weak:   return "possible tail";
    case Verdict::Likely: return "likely tail";
    case Verdict::Strong: return "rotating tail";
  }
  return "unknown";
}

}  // namespace TrackerFollow
