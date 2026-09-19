#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Spotter signatures — what the detector looks for.
 *
 * Kept apart from the detector itself because this is the part that goes
 * stale. Vendors change contract manufacturers, buy new OUI blocks and
 * revise firmware; the matching logic does not. Editing this file should
 * never require touching Spotter.cpp.
 *
 * Everything here is passive. These are things a device broadcasts on its
 * own: WiFi probe requests it sends looking for a home network, beacons, and
 * BLE advertisements. Nothing in Spotter transmits.
 *
 * Sources, so a future reader can re-check rather than trust:
 *   B4:1E:52     Flock Safety's own IEEE MA-L assignment
 *   00:03:7F     Qualcomm Atheros (QCA9377), the radio in several units
 *   E4:AA:EA     Liteon, a contract manufacturer seen on these
 *   0x0D53       Luxottica, BLE company ID in Meta Ray-Ban advertisements
 *   0xFD5F       Meta, BLE service UUID in the same advertisements
 *
 * Community OUI collections worth pulling from as they grow:
 *   github.com/colonelpanichacks/flock-you
 *   github.com/simeononsecurity/flock-finder
 *
 * A word on confidence. An OUI alone is weak evidence: blocks get resold and
 * contract manufacturers build for everyone, so a Qualcomm OUI means "this
 * might be the right radio", not "this is a camera". Two independent fields
 * agreeing is strong. The detector scores accordingly and the UI shows it,
 * because a detector that cries wolf is one you stop believing.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>   // size_t, for the table-length constants below
#include <stdint.h>

namespace Spotter {

enum class Kind : uint8_t { Unknown = 0, Alpr, Glasses, Accessory };

/* How much a single match is worth. Two weak hits on one device promote it. */
enum class Conf : uint8_t { Weak = 0, Likely, Strong };

struct OuiSig {
  uint8_t oui[3];
  Kind kind;
  Conf conf;
  const char* label;
};

struct NameSig {
  const char* prefix;   // matched case-insensitively against SSID or BLE name
  Kind kind;
  Conf conf;
  const char* label;
};

struct BleSig {
  uint16_t company;     // BLE manufacturer company ID, 0 = don't care
  uint16_t service;     // 16-bit service UUID, 0 = don't care
  Kind kind;
  Conf conf;
  const char* label;
};

/* ── WiFi: source MAC prefixes ───────────────────────────────────────────── */
static const OuiSig kOuiSigs[] = {
  /* Flock Safety's own block. Strong on its own. */
  {{0xB4, 0x1E, 0x52}, Kind::Alpr, Conf::Strong,  "Flock Safety"},

  /* Radio and contract-manufacturer blocks. These appear in ALPR units but
   * also in plenty of unrelated hardware, so on their own they are a hint,
   * not a finding. */
  {{0x00, 0x03, 0x7F}, Kind::Alpr, Conf::Weak,    "Atheros QCA9377"},
  {{0xE4, 0xAA, 0xEA}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
};

/* ── WiFi: SSID patterns in probe requests and beacons ───────────────────── */
static const NameSig kSsidSigs[] = {
  {"Flock-",   Kind::Alpr,      Conf::Strong, "Flock SSID"},
  {"Penguin-", Kind::Accessory, Conf::Likely, "Flock battery pack"},
};

/* ── BLE: advertisement contents ─────────────────────────────────────────── */
static const BleSig kBleSigs[] = {
  /* Company and service together. Two independent fields agreeing is about
   * as good as passive identification gets. */
  {0x0D53, 0xFD5F, Kind::Glasses, Conf::Strong, "Meta Ray-Ban"},

  /* Either field alone: same hardware family, less certainty. */
  {0x0D53, 0x0000, Kind::Glasses, Conf::Likely, "Luxottica eyewear"},
  {0x0000, 0xFD5F, Kind::Glasses, Conf::Likely, "Meta device"},
};

/* ── BLE: advertised names ───────────────────────────────────────────────── */
static const NameSig kBleNameSigs[] = {
  {"Penguin-",    Kind::Accessory, Conf::Likely, "Flock battery pack"},
  {"Ray-Ban",     Kind::Glasses,   Conf::Strong, "Ray-Ban Meta"},
  {"Spectacles",  Kind::Glasses,   Conf::Strong, "Snap Spectacles"},
};

constexpr size_t kOuiSigCount     = sizeof(kOuiSigs) / sizeof(kOuiSigs[0]);
constexpr size_t kSsidSigCount    = sizeof(kSsidSigs) / sizeof(kSsidSigs[0]);
constexpr size_t kBleSigCount     = sizeof(kBleSigs) / sizeof(kBleSigs[0]);
constexpr size_t kBleNameSigCount = sizeof(kBleNameSigs) / sizeof(kBleNameSigs[0]);

}  // namespace Spotter
