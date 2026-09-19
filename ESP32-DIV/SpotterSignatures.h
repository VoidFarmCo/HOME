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
 *   00:25:DF     Axon Enterprise's own IEEE MA-L assignment
 *   00:03:7F     Qualcomm Atheros (QCA9377), the radio in several units
 *   0x0D53       Luxottica, BLE company ID in Meta Ray-Ban advertisements
 *   0xFD5F       Meta, BLE service UUID in the same advertisements
 *   0x09C8       XUNTONG, company ID in Penguin battery advertisements
 *
 * The bulk OUI list below is the union of two community collections:
 *   github.com/colonelpanichacks/flock-you      (32 prefixes)
 *   github.com/simeononsecurity/flock-finder    (31 prefixes)
 * 30 of those are common to both; the union is 33. Every one was resolved
 * against the IEEE MA-L registry (standards-oui.ieee.org/oui/oui.csv) before
 * being written down here, and each carries the assignee the registry
 * actually names rather than the word "Flock".
 *
 * That check is the reason they are nearly all Weak. Twenty-three of the 33
 * belong to Liteon and two to Espressif -- contract manufacturers and a
 * module vendor whose blocks are in an enormous amount of unrelated consumer
 * hardware. Reported as a camera on their own they would be wrong far more
 * often than right. What they are good for is corroboration: a Weak OUI on
 * the same MAC as a "Flock-" SSID or a Penguin advertisement is what turns a
 * guess into a finding.
 *
 * Two entries are worth knowing about before trusting them. B8:35:32 is not
 * registered to anyone. 82:6B:F2 has the locally-administered bit set, so it
 * is a randomised address rather than a vendor block -- flock-finder reports
 * newer units using locally-administered MACs specifically to defeat OUI
 * matching, which is also why a recurring one is interesting enough to keep.
 *
 * A word on confidence. An OUI alone is weak evidence: blocks get resold and
 * contract manufacturers build for everyone, so a Qualcomm OUI means "this
 * might be the right radio", not "this is a camera". Two independent fields
 * agreeing is strong. The detector scores accordingly and the UI shows it,
 * because a detector that cries wolf is one you stop believing.
 *
 * Not expressible here, and so deliberately left out rather than fudged:
 * the Flock accessory GATT service e8ccbb38-9532-46a8-9fe5-1814df172e6f and
 * the Nordic DFU service, both 128-bit, against a BleSig.service that is
 * uint16_t; and the Bluetooth Classic names, which Spotter does not scan for
 * at all. Picking either up means changing the struct or the scanner, not
 * this table.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stddef.h>   // size_t, for the table-length constants below
#include <stdint.h>

namespace Spotter {

enum class Kind : uint8_t { Unknown = 0, Alpr, Glasses, Bodycam, Accessory };

/* How much a single match is worth. Corroboration -- a second, differently
 * labelled signature on the same MAC -- promotes Likely to Strong. It does
 * not promote Weak: two contract-manufacturer hints agreeing are still two
 * hints. See record() in Spotter.cpp. */
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

/* ── WiFi: source MAC prefixes ─────────────────────────────────────── */
/* The scan breaks on the first match, so anything that should outrank a
 * generic block has to sit above it. */
static const OuiSig kOuiSigs[] = {
  /* Flock Safety's own block. Strong on its own. */
  {{0xB4, 0x1E, 0x52}, Kind::Alpr, Conf::Strong,  "Flock Safety"},

  /* The radio in several units. Qualcomm builds for everyone. */
  {{0x00, 0x03, 0x7F}, Kind::Alpr, Conf::Weak,    "Atheros QCA9377"},

  /* Axon Enterprise's own IEEE block, and the only body-camera signature
   * here. Strong on the vendor, not on the product: Axon also builds the
   * docks, the in-car Fleet systems and the TASERs, so this says Axon
   * hardware is in range rather than specifically a camera on a shoulder.
   * Axon Body units carry WiFi for dock upload and Axon View, which is what
   * makes them audible at all. Unlike the block below it, this one is the
   * vendor's own assignment rather than a contract manufacturer's. */
  {{0x00, 0x25, 0xDF}, Kind::Bodycam, Conf::Strong, "Axon Enterprise"},

  /* Liteon Technology Corporation -- 23 blocks across the two lists. */
  {{0x00, 0xF4, 0x8D}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x14, 0x5A, 0xFC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x14, 0xB5, 0xCD}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x24, 0xB2, 0xB9}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x3C, 0x91, 0x80}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x58, 0x00, 0xE3}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x5C, 0x93, 0xA2}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x64, 0x6E, 0x69}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x70, 0x08, 0x94}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x70, 0xC9, 0x4E}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x74, 0x4C, 0xA1}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x80, 0x30, 0x49}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x94, 0x08, 0x53}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0x9C, 0x2F, 0x9D}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xB8, 0x1E, 0xA4}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xC0, 0x35, 0x32}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xD0, 0x39, 0x57}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xD8, 0xF3, 0xBC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE0, 0x0A, 0xF6}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE4, 0xAA, 0xEA}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xE8, 0xD0, 0xFC}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xF4, 0x6A, 0xDD}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},
  {{0xF8, 0xA2, 0xD6}, Kind::Alpr, Conf::Weak,    "Liteon (ALPR?)"},

  /* Silicon Laboratories -- 3 blocks across the two lists. */
  {{0x58, 0x8E, 0x81}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},
  {{0x90, 0x35, 0xEA}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},
  {{0xEC, 0x1B, 0xBD}, Kind::Alpr, Conf::Weak,    "SiLabs (ALPR?)"},

  /* Espressif Inc -- 2 blocks across the two lists. */
  {{0x3C, 0x71, 0xBF}, Kind::Alpr, Conf::Weak,    "Espressif (ALPR?)"},
  {{0xA4, 0xCF, 0x12}, Kind::Alpr, Conf::Weak,    "Espressif (ALPR?)"},

  /* Universal Global Scientific Industrial (USI) -- 2 blocks. */
  {{0x08, 0x3A, 0x88}, Kind::Alpr, Conf::Weak,    "USI (ALPR?)"},
  {{0xE0, 0x4F, 0x43}, Kind::Alpr, Conf::Weak,    "USI (ALPR?)"},

  /* Samsung Electronics -- 1 block. */
  {{0x48, 0x27, 0xEA}, Kind::Alpr, Conf::Weak,    "Samsung (ALPR?)"},

  /* Neither of these is a vendor block. B8:35:32 is in no IEEE registry;
   * 82:6B:F2 has the locally-administered bit set, which makes it a
   * randomised address that happens to keep recurring. */
  {{0x82, 0x6B, 0xF2}, Kind::Alpr, Conf::Weak,    "LAA, not a vendor"},
  {{0xB8, 0x35, 0x32}, Kind::Alpr, Conf::Weak,    "unregistered OUI"},
};

/* ── WiFi: SSID patterns in probe requests and beacons ───────────────────── */
/* Ordered most specific first: prefixMatch stops at the first hit, so a bare
 * "Flock" above "Flock-" would swallow the provisioning SSIDs and report them
 * at the lower confidence. */
static const NameSig kSsidSigs[] = {
  {"Flock Camera net", Kind::Alpr, Conf::Strong, "Flock camera SSID"},
  {"Flock-",           Kind::Alpr, Conf::Strong, "Flock SSID"},

  /* Provisioned units drop the suffix. Still specific, but a bare word is a
   * bare word and somebody's home network can be called this. */
  {"Flock",            Kind::Alpr, Conf::Likely, "Flock (bare)"},

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

  /* The Penguin battery pack's advertisements carry XUNTONG's company ID
   * with a serial in the payload. SIG company IDs are assigned, so this is
   * a good deal more specific than an OUI. */
  {0x09C8, 0x0000, Kind::Accessory, Conf::Likely, "XUNTONG (Penguin)"},

  /* Raven camera GATT. The documented range is 0x3100-0x3500 and the struct
   * matches one value at a time, so these are the three that are actually
   * named as carrying data. They are 16-bit UUIDs outside the SIG-assigned
   * space, where any vendor may collide, hence Weak. */
  {0x0000, 0x3100, Kind::Alpr, Conf::Weak, "Raven GATT 3100"},
  {0x0000, 0x3101, Kind::Alpr, Conf::Weak, "Raven GATT 3101"},
  {0x0000, 0x3102, Kind::Alpr, Conf::Weak, "Raven GATT 3102"},
};

/* ── BLE: advertised names ───────────────────────────────────────────────── */
static const NameSig kBleNameSigs[] = {
  {"Penguin-",       Kind::Accessory, Conf::Likely, "Flock battery pack"},
  {"FS Ext Battery", Kind::Accessory, Conf::Strong, "Flock ext battery"},
  {"Ray-Ban",        Kind::Glasses,   Conf::Strong, "Ray-Ban Meta"},
  {"Spectacles",     Kind::Glasses,   Conf::Strong, "Snap Spectacles"},

  /* The stock Nordic bootloader name. It is in the community lists because
   * the battery pack advertises it while updating, but so does every other
   * Nordic device in DFU mode, so on its own it means almost nothing. Kept
   * because it costs one row and it corroborates a Penguin sitting next to
   * it. */
  {"DfuTarg",        Kind::Accessory, Conf::Weak,   "Nordic DFU (generic)"},
};

constexpr size_t kOuiSigCount     = sizeof(kOuiSigs) / sizeof(kOuiSigs[0]);
constexpr size_t kSsidSigCount    = sizeof(kSsidSigs) / sizeof(kSsidSigs[0]);
constexpr size_t kBleSigCount     = sizeof(kBleSigs) / sizeof(kBleSigs[0]);
constexpr size_t kBleNameSigCount = sizeof(kBleNameSigs) / sizeof(kBleNameSigs[0]);

}  // namespace Spotter
