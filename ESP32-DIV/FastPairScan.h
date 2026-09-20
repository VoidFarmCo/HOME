#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * FastPairScan — the Fast Pair feature: listen, then optionally probe.
 *
 * Two halves, and they are deliberately not the same act.
 *
 * Listening is passive. It is a BLE scan with active scanning off, reading
 * service data under 0xFE2C that every Fast Pair device broadcasts without
 * being asked. Same posture as Spotter and the AirTag sniffer.
 *
 * Probing transmits. It opens a GATT connection to one device the operator
 * selected and writes eighty bytes to one characteristic to test whether
 * that device answers a handshake it should refuse (CVE-2025-36911). It is
 * behind a deliberate confirm step, it never runs on its own, and it never
 * runs against more than the one selected row. See FastPairProbe.h for what
 * it sends and docs/pueo/fast-pair-probe.md for what the answer means.
 *
 * Rows are keyed on BLE address and nothing else. When a device rotates its
 * address it becomes a new row, which looks worse and is correct: Fast Pair
 * offers a passive listener no stable per-unit identifier, so merging rows
 * would mean merging on a model ID or a name, and both of those merge
 * strangers' devices together. docs/pueo/fast-pair.md sets this out.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace FastPairScan {

void fastPairSetup();
void fastPairLoop();
void exit();

/* Exposed for logging and for anything that wants the counts. */
int      deviceCount();
uint32_t advertsSeen();

}  // namespace FastPairScan
