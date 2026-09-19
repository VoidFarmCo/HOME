#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Spotter — passive detection of nearby surveillance hardware.
 *
 * Two things it looks for, both of which announce themselves without being
 * asked:
 *
 *   ALPR cameras   broadcast WiFi probe requests looking for the network
 *                  they upload to. They do this whether or not anyone is
 *                  listening and whether or not a hotspot is present, so a
 *                  receiver hears them without transmitting anything.
 *
 *   Smart glasses  advertise over BLE, and Meta's put both a Luxottica
 *                  company ID and a Meta service UUID in the same packet.
 *
 *   Body cameras   carry WiFi so they can offload to a dock, which makes an
 *                  Axon unit audible the same way an ALPR camera is.
 *
 * Receive only. Spotter never transmits, never associates, never deauths.
 * It puts the radio in promiscuous mode and runs a passive BLE scan, which
 * is the same thing any WiFi analyser does. That matters legally as well as
 * technically: listening to broadcasts is a different act from injecting
 * frames, and this feature deliberately stays on the listening side.
 *
 * Signatures live in SpotterSignatures.h and are meant to be edited.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>
#include "SpotterSignatures.h"

namespace Spotter {

/** One device we have heard from at least once. */
struct Hit {
  uint8_t  mac[6];
  Kind     kind;
  Conf     conf;
  const char* label;    // points into the signature tables; never freed
  int8_t   rssiBest;
  int8_t   rssiLast;
  uint32_t firstMs;
  uint32_t lastMs;
  uint16_t hits;
  bool     viaBle;      // false = seen on WiFi
  bool     corroborated; // matched on more than one signature
  /* Hash of the device's probe-request element set, or 0 if none has been
   * seen. Survives the MAC changing, which the address above does not.
   * WiFi only; a BLE row never has one. See docs/pueo/ie-fingerprinting.md. */
  uint32_t fingerprint;
  /* How many times this row has been seen wearing a different address. Only
   * ever non-zero for randomised addresses; see findOrAdd. */
  uint8_t  addrChanges;
};

/* Feature entry points, following the shape every other module here uses. */
void spotterSetup();
void spotterLoop();
void exit();

/* Capture to SD. Off until the operator turns it on, and see the note in
 * Spotter.cpp about what a capture of the air around you contains. */
bool        captureActive();
uint32_t    captureRows();

/* Exposed for the UI and for logging. */
int         hitCount();
const Hit*  hitAt(int i);
uint32_t    framesSeen();
uint8_t     currentChannel();

}  // namespace Spotter
