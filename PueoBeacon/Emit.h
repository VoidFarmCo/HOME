#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Emit — the bench transmitter behind Pueo's receivers.
 *
 * Every detector in Pueo is asserted against synthetic frames and a reference
 * implementation, and none of that proves a radio path. A decoder can be
 * perfect while the scan never delivers it a byte. This emits, on real
 * radios, the things Pueo listens for, so "nothing found" can be told apart
 * from "nothing works".
 *
 * ── Why it is a separate image ──────────────────────────────────────────────
 *
 * Spotter and DroneScan both say, in their headers and on the website, that
 * they transmit nothing. That is a claim about the binary as much as about
 * the feature. Building the imitator into the same image would make it false
 * of the binary while staying true of the feature, which is the kind of
 * distinction nobody should have to make. PUEO_ROLE=beacon is a different
 * firmware for a different board.
 *
 * ── Why the payloads are unmistakably fake ──────────────────────────────────
 *
 * These signals do not stay on the bench just because that is where the
 * bench is. Remote ID in particular is an airspace-awareness broadcast with
 * public receiver apps, and a fabricated aircraft is a fabricated aircraft in
 * somebody else's tool. A fake plate-reader signature lands the same way in
 * somebody else's anti-surveillance detector.
 *
 * So the defaults are built to be recognisable rather than to be convincing:
 *
 *   - identifiers say PUEO-TEST, not a plausible serial
 *   - addresses come from the IEEE documentation range where one exists,
 *     and are locally administered otherwise
 *   - transmit power is set to the floor
 *   - it stops on its own after kAutoStopMs
 *
 * That is deliberate. A test kit that is indistinguishable from the real
 * thing is not a better test kit, it is a worse one: you cannot tell your
 * own decoy from the article you were trying to detect.
 * ────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Emit {

/* Long enough to walk to the other board and read a screen; short enough
 * that a unit left powered on a bench stops shouting by itself. */
constexpr uint32_t kAutoStopMs = 10u * 60u * 1000u;

enum Signal : uint8_t {
  RemoteIdWifi = 0,   // DroneScan, Wi-Fi Beacon path
  RemoteIdBle,        // DroneScan, BLE 4 legacy path
  AlprProbe,          // Spotter, WiFi probe request with a plate-reader OUI
  BodycamBeacon,      // Spotter, WiFi beacon with a body-camera OUI
  GlassesBle,         // Spotter, BLE company + service pair
  VehicleBle,         // Spotter, BLE advertised name
  TrackerBle,         // Hunt and AirTag Sniffer, Find My offline finding
  FastPairBle,        // FastPairScan, service data 0xFE2C
  kSignalCount
};

const char* name(Signal s);
const char* detectedBy(Signal s);

/* Bring the radios up. Called once. */
void begin();

/* Send one burst of `s`. Cheap and synchronous; the caller paces it. */
void send(Signal s);

/* Which BLE signal is currently being advertised, or kSignalCount for none.
 * BLE advertising is a state rather than an event, so only one BLE signal
 * can be live at a time and the scheduler rotates them. */
Signal currentBle();

/* Stop advertising and drop the Wi-Fi interface. */
void allStop();

/* Counters, for the screen. */
uint32_t sentCount(Signal s);

/* Frames esp_wifi_80211_tx refused. Non-zero means nothing is going out on
 * Wi-Fi no matter what the sent counters say. */
uint32_t txFailures();

}  // namespace Emit
