#include "Emit.h"

/* The detector's own headers, included rather than copied.
 *
 * Every constant below that has to match something is taken from the file
 * the detector reads it from. A decoy built from a second copy of the
 * numbers tests the copy, not the detector: the two drift, the bench goes
 * green, and the thing it was meant to prove is exactly what stopped being
 * true. tools/check_beacon.py asserts these paths still resolve. */
#include "DroneId.h"
#include "FastPair.h"
#include "SpotterSignatures.h"

#include <NimBLEDevice.h>
#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

/* ── why raw injection needs this ────────────────────────────────────────
 *
 * esp_wifi_80211_tx refuses frames the IDF does not like the look of, and a
 * hand-built beacon or probe request is exactly that. The IDF's check is
 * overridden with one that says yes; build.sh weakens the library's copy of
 * the symbol so this strong definition wins the link.
 *
 * The detector has had this in wifi.cpp since upstream. The beacon is a
 * separate sketch and did not inherit it, so every Wi-Fi frame this emitted
 * was rejected before it reached the air -- while BLE, which does not go
 * through this path, worked perfectly. The symptom was a bench where the
 * glasses decoy was found and the plate reader and body camera never were,
 * which reads like two broken detectors rather than one missing function. */
extern "C" int ieee80211_raw_frame_sanity_check(int32_t, int32_t, int32_t) {
  return 0;
}

namespace Emit {
namespace {

uint32_t s_sent[kSignalCount];
uint32_t s_txFail = 0;
Signal   s_bleLive = kSignalCount;
bool     s_wifiUp  = false;

/* ── addresses ────────────────────────────────────────────────────────────
 *
 * An OUI matcher cannot be tested without the OUI it matches, so the vendor
 * blocks below are the real ones -- that is inherent to the exercise, not a
 * shortcut. What is not inherent is the rest of the address, so the device
 * half spells DEAD and the SSIDs say PUEO-TEST. A capture of this is
 * identifiable as a decoy by anyone who looks at more than the first three
 * bytes. */
uint8_t s_alprMac[6]    = {0xB4, 0x1E, 0x52, 0xDE, 0xAD, 0x01};  // Flock Safety
uint8_t s_bodycamMac[6] = {0x00, 0x25, 0xDF, 0xDE, 0xAD, 0x02};  // Axon

/* Deliberately an OUI that matches nothing in SpotterSignatures.h.
 *
 * A real Pineapple often has an ALFA radio in it, and 00:C0:CA is in the
 * table as a Weak "Pineapple radio?" guess, so using it here would be the
 * more realistic decoy. It would also make the test useless: the hit would
 * be corroborated by two signatures and there would be no way to tell from
 * the screen whether the new substring rule fired at all. The locally
 * administered 0x02 prefix matches nothing, so a hit here is the SSID rule
 * or it is nothing. */
uint8_t s_pentestMac[6] = {0x02, 0xDE, 0xAD, 0xDE, 0xAD, 0x03};

constexpr char kTestSsid[] = "PUEO-TEST-DECOY";

/* The needle has to be in the middle, because that is the rule being
 * tested: NameSig would match this as a prefix and the point is that it
 * cannot. Everything around it still says what this is. */
constexpr char kPentestSsid[] = "PUEO-TEST-Pineapple_DECOY";

/* ── Wi-Fi ────────────────────────────────────────────────────────────────
 *
 * Raw injection, the same esp_wifi_80211_tx path the beacon-spam feature
 * uses. The interface has to exist and be on a channel before a frame will
 * go out; nothing is associated and nothing is served. */
void wifiUp() {
  if (s_wifiUp) return;
  WiFi.mode(WIFI_AP);
  /* A hidden AP with a throwaway SSID: the interface is needed, the network
   * is not. Channel 6 because it is where a scanner sits longest. */
  WiFi.softAP("pueo-bench", nullptr, 6, 1 /* hidden */, 1);
  esp_wifi_set_max_tx_power(8);   // the floor, ~2 dBm: a bench, not a hilltop
  s_wifiUp = true;
}

uint8_t s_frame[256];

/* 802.11 header, shared by the beacon and the probe request. */
/* Send, and count a refusal. The first version of this discarded the return
 * value, so a transmitter that was emitting nothing at all looked exactly
 * like a transmitter nobody was listening to. */
bool tx(const uint8_t* frame, size_t len) {
  const esp_err_t r = esp_wifi_80211_tx(WIFI_IF_AP, frame, (int)len, false);
  if (r != ESP_OK) {
    s_txFail++;
    return false;
  }
  return true;
}

size_t hdr(uint8_t subtype, const uint8_t* src, const uint8_t* dst) {
  memset(s_frame, 0, sizeof(s_frame));
  s_frame[0] = subtype;
  memcpy(s_frame + 4,  dst, 6);
  memcpy(s_frame + 10, src, 6);
  memcpy(s_frame + 16, src, 6);   // BSSID = transmitter
  return 24;
}

size_t addSsid(size_t at, const char* ssid) {
  const size_t n = strlen(ssid);
  s_frame[at] = 0x00;
  s_frame[at + 1] = (uint8_t)n;
  memcpy(s_frame + at + 2, ssid, n);
  return at + 2 + n;
}

size_t addRates(size_t at) {
  static const uint8_t rates[] = {0x82, 0x84, 0x8B, 0x96, 0x24, 0x30, 0x48, 0x6C};
  s_frame[at] = 0x01;
  s_frame[at + 1] = sizeof(rates);
  memcpy(s_frame + at + 2, rates, sizeof(rates));
  return at + 2 + sizeof(rates);
}

/* ── Remote ID messages ───────────────────────────────────────────────────
 *
 * Built to the layouts in DroneId.h, with the scale factors run backwards.
 * The identifier is the point: PUEO-TEST-0001 is not a serial anybody could
 * mistake for an aircraft's. */
uint8_t s_ridCounter = 0;

size_t buildBasicId(uint8_t* m) {
  memset(m, 0, DroneId::kMessageSize);
  m[0] = (uint8_t)((DroneId::BasicId << 4) | DroneId::kProtocolVersion);
  m[1] = (uint8_t)((1 << 4) | 2);          // ID type 1 serial, UA type 2 multirotor
  const char* id = "PUEO-TEST-0001";
  memcpy(m + 2, id, strlen(id));
  return DroneId::kMessageSize;
}

size_t buildLocation(uint8_t* m) {
  memset(m, 0, DroneId::kMessageSize);
  m[0] = (uint8_t)((DroneId::Location << 4) | DroneId::kProtocolVersion);
  m[1] = (uint8_t)((2 << 4) | 0);          // status 2 airborne, no mult, east
  m[2] = 90;                               // heading 90
  m[3] = 80;                               // 80 * 0.25 = 20 m/s
  m[4] = (uint8_t)(int8_t)(-10);           // -10 * 0.5 = -5 m/s
  /* Somewhere obviously nowhere: 0.1 N 0.1 E is in the Gulf of Guinea, which
   * DroneScan treats as a real fix only because it is not exactly 0,0 -- and
   * which no aircraft is going to be mistaken for. */
  const int32_t lat = 1000000, lon = 1000000;
  memcpy(m + 5, &lat, 4);
  memcpy(m + 9, &lon, 4);
  const uint16_t alt = (uint16_t)((100.0f + 1000.0f) / 0.5f);   // 100 m
  memcpy(m + 15, &alt, 2);
  memcpy(m + 17, &alt, 2);
  const uint16_t ts = 1234;                // 123.4 s into the hour
  memcpy(m + 21, &ts, 2);
  return DroneId::kMessageSize;
}

size_t buildOperatorId(uint8_t* m) {
  memset(m, 0, DroneId::kMessageSize);
  m[0] = (uint8_t)((DroneId::OperatorId << 4) | DroneId::kProtocolVersion);
  m[1] = 0;
  const char* id = "PUEO-TEST-OP";
  memcpy(m + 2, id, strlen(id));
  return DroneId::kMessageSize;
}

/* A pack, so the pack path gets exercised too and not just single messages. */
size_t buildPack(uint8_t* out) {
  out[0] = (uint8_t)((DroneId::Packed << 4) | DroneId::kProtocolVersion);
  out[1] = (uint8_t)DroneId::kMessageSize;
  out[2] = 3;
  buildBasicId(out + 3);
  buildLocation(out + 3 + DroneId::kMessageSize);
  buildOperatorId(out + 3 + 2 * DroneId::kMessageSize);
  return 3 + 3 * DroneId::kMessageSize;
}

/* ── BLE ──────────────────────────────────────────────────────────────────
 *
 * One advertisement at a time, because advertising is a state. The scheduler
 * in the sketch rotates which one is live.
 *
 * Each decoy advertises from its own address, and that is not cosmetic.
 * Spotter keys a row on the MAC and keeps the strongest signature that row
 * has matched: one address sending both the Meta glasses pair (Strong) and
 * the KARR vehicle name (Likely) becomes a single row labelled Meta
 * Ray-Ban, marked corroborated, with the vehicle match folded invisibly
 * into it. The detector is right to do that -- one device is not credibly
 * both -- so the bench has to stop pretending it is one device.
 *
 * Random static, which is what the top two bits of the most significant
 * byte mean. The address goes to the controller least significant byte
 * first, so that byte is addr[5]. */
void setBleAddress(uint8_t tag) {
  uint8_t addr[6] = {tag, 0xAD, 0xDE, 0x55, 0x50, 0xC0};
  ble_hs_id_set_rnd(addr);
}

void bleAdvertise(const std::string& payload, uint8_t tag) {
  NimBLEAdvertising* adv = NimBLEDevice::getAdvertising();
  adv->stop();
  setBleAddress(tag);
  NimBLEAdvertisementData d;
  d.addData(payload);
  adv->setAdvertisementData(d);
  adv->setMinInterval(160);
  adv->setMaxInterval(160);
  adv->start();
}

/* A service-data AD structure: length, type 0x16, UUID little-endian, body. */
std::string serviceData16(uint16_t uuid, const uint8_t* body, size_t n) {
  std::string s;
  s.push_back((char)(uint8_t)(3 + n));
  s.push_back((char)0x16);
  s.push_back((char)(uint8_t)(uuid & 0xFF));
  s.push_back((char)(uint8_t)(uuid >> 8));
  s.append((const char*)body, n);
  return s;
}

/* A manufacturer-data AD structure: length, type 0xFF, company LE, body. */
std::string mfgData(uint16_t company, const uint8_t* body, size_t n) {
  std::string s;
  s.push_back((char)(uint8_t)(3 + n));
  s.push_back((char)0xFF);
  s.push_back((char)(uint8_t)(company & 0xFF));
  s.push_back((char)(uint8_t)(company >> 8));
  s.append((const char*)body, n);
  return s;
}

std::string completeName(const char* name) {
  const size_t n = strlen(name);
  std::string s;
  s.push_back((char)(uint8_t)(1 + n));
  s.push_back((char)0x09);            // Complete Local Name
  s.append(name, n);
  return s;
}

std::string uuid16(uint16_t u) {
  std::string s;
  s.push_back((char)0x03);
  s.push_back((char)0x03);            // Complete List of 16-bit Service UUIDs
  s.push_back((char)(uint8_t)(u & 0xFF));
  s.push_back((char)(uint8_t)(u >> 8));
  return s;
}

}  // namespace

const char* name(Signal s) {
  switch (s) {
    case RemoteIdWifi:  return "Remote ID / WiFi";
    case RemoteIdBle:   return "Remote ID / BLE";
    case AlprProbe:     return "ALPR probe";
    case BodycamBeacon: return "Bodycam beacon";
    case GlassesBle:    return "Smart glasses";
    case VehicleBle:    return "Vehicle module";
    case TrackerBle:    return "Find My tracker";
    case FindHubBle:    return "Find Hub tag";
    case DultBle:       return "DULT tracker";
    case PentestWifi:   return "Pineapple SSID";
    case FastPairBle:   return "Fast Pair";
    default:            return "?";
  }
}

const char* detectedBy(Signal s) {
  switch (s) {
    case RemoteIdWifi:
    case RemoteIdBle:   return "Drones";
    case AlprProbe:
    case BodycamBeacon:
    case GlassesBle:
    case VehicleBle:    return "Spotter";
    case TrackerBle:    return "Hunt / AirTag Sniffer";
    case FindHubBle:
    case DultBle:
    case PentestWifi:   return "Spotter";
    case FastPairBle:   return "Fast Pair";
    default:            return "";
  }
}

void begin() {
  memset(s_sent, 0, sizeof(s_sent));
  wifiUp();
  NimBLEDevice::init("");
  NimBLEDevice::setOwnAddrType(BLE_OWN_ADDR_RANDOM);
  NimBLEDevice::setPower(ESP_PWR_LVL_N12);   // floor
}

void send(Signal s) {
  switch (s) {
    case RemoteIdWifi: {
      size_t at = hdr(0x80, s_alprMac, (const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
      /* The transmitter for a drone beacon should not wear a plate-reader's
       * OUI. Locally administered, and obviously a decoy. */
      static uint8_t droneMac[6] = {0x02, 0x50, 0x55, 0xDE, 0xAD, 0x03};
      memcpy(s_frame + 10, droneMac, 6);
      memcpy(s_frame + 16, droneMac, 6);
      at += 12;                              // timestamp, interval, capability
      s_frame[at - 4] = 0x64;                // beacon interval 100 TU
      at = addSsid(at, kTestSsid);
      at = addRates(at);

      uint8_t body[3 + 3 * DroneId::kMessageSize];
      const size_t bodyLen = buildPack(body);
      s_frame[at++] = DroneId::kWifiElementId;
      s_frame[at++] = (uint8_t)(4 + 1 + bodyLen);   // OUI+type, counter, body
      memcpy(s_frame + at, DroneId::kWifiOui, 3); at += 3;
      s_frame[at++] = DroneId::kWifiOuiType;
      s_frame[at++] = s_ridCounter++;
      memcpy(s_frame + at, body, bodyLen); at += bodyLen;

      if (!tx(s_frame, at)) return;
      break;
    }

    case AlprProbe: {
      size_t at = hdr(0x40, s_alprMac, (const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
      at = addSsid(at, kTestSsid);
      at = addRates(at);
      if (!tx(s_frame, at)) return;
      break;
    }

    case BodycamBeacon: {
      size_t at = hdr(0x80, s_bodycamMac, (const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
      at += 12;
      s_frame[at - 4] = 0x64;
      at = addSsid(at, kTestSsid);
      at = addRates(at);
      if (!tx(s_frame, at)) return;
      break;
    }

    case RemoteIdBle: {
      uint8_t body[2 + DroneId::kMessageSize];
      body[0] = DroneId::kBleAppCode;
      body[1] = s_ridCounter++;
      /* One message per advertisement: 25 bytes plus the header already
       * fills a legacy 31-byte payload, which is why the standard rotates
       * message types rather than packing them here. */
      static uint8_t which = 0;
      if (which == 0)      buildBasicId(body + 2);
      else if (which == 1) buildLocation(body + 2);
      else                 buildOperatorId(body + 2);
      which = (uint8_t)((which + 1) % 3);
      bleAdvertise(serviceData16(DroneId::kBleServiceUuid, body, sizeof(body)), s);
      s_bleLive = s;
      break;
    }

    case GlassesBle: {
      /* Company and service from kBleSigs: Luxottica 0x0D53 with Meta's
       * 0xFD5F is the Strong pair Spotter calls "Meta Ray-Ban". */
      const uint8_t body[] = {0x01, 0x00};
      bleAdvertise(uuid16(0xFD5F) + mfgData(0x0D53, body, sizeof(body)), s);
      s_bleLive = s;
      break;
    }

    case VehicleBle: {
      /* kNameSigs wants the prefix "QT " at exactly 11 characters. */
      bleAdvertise(completeName("QT 12345678"), s);
      s_bleLive = s;
      break;
    }

    case TrackerBle: {
      /* Apple offline finding: company 0x004C, Continuity type 0x12. The
       * payload after the type is an opaque key; this one is PUEO in it. */
      uint8_t body[24] = {0};
      body[0] = 0x12;                    // Find My
      body[1] = 0x19;                    // length of what follows
      body[2] = 0x00;                    // status
      memcpy(body + 3, "PUEO-TEST-KEY", 13);
      bleAdvertise(mfgData(0x004C, body, sizeof(body)), s);
      s_bleLive = s;
      break;
    }

    case FindHubBle: {
      /* Google Find My Device network: service 0xFEAA, frame type 0x40.
       *
       * The frame type is the whole point. Pueo has 0xFEAA as a Weak
       * "Eddystone beacon" and could not do better, so a Find Hub tag read
       * as shop furniture. This decoy fires both rules at once, which is
       * the useful case to watch: the Strong tracker label has to win. */
      uint8_t body[18] = {0};
      body[0] = 0x40;                    // Find My Device frame
      memcpy(body + 1, "PUEO-TEST-KEY", 13);
      bleAdvertise(serviceData16(0xFEAA, body, sizeof(body)), s);
      s_bleLive = s;
      break;
    }

    case DultBle: {
      /* IETF DULT, service 0xFCB2: a tracker telling any phone in earshot
       * that it is separated from its owner. No frame type, because the
       * service alone is the declaration. */
      uint8_t body[16] = {0};
      memcpy(body, "PUEO-TEST-DULT", 14);
      bleAdvertise(serviceData16(0xFCB2, body, sizeof(body)), s);
      s_bleLive = s;
      break;
    }

    case PentestWifi: {
      /* A beacon whose SSID carries the vendor in the middle. s_pentestMac
       * matches no OUI rule on purpose, so a hit is the substring rule. */
      size_t at = hdr(0x80, s_pentestMac, (const uint8_t*)"\xFF\xFF\xFF\xFF\xFF\xFF");
      at += 12;
      s_frame[at - 4] = 0x64;
      at = addSsid(at, kPentestSsid);
      at = addRates(at);
      if (!tx(s_frame, at)) return;
      break;
    }

    case FastPairBle: {
      /* Three bytes is the discoverable shape: a 24-bit Model ID. Anything
       * else is not-discoverable and carries no Model ID at all, which is
       * the distinction FastPair.cpp exists to get right -- so this
       * alternates, to exercise both. */
      static bool discoverable = true;
      if (discoverable) {
        const uint8_t model[3] = {0x2B, 0x71, 0xB2};
        bleAdvertise(serviceData16(FastPair::kUuidFastPair, model, sizeof(model)), s);
      } else {
        const uint8_t nd[] = {0x00, 0x11, 0x22, 0x33, 0x44, 0x55};
        bleAdvertise(serviceData16(FastPair::kUuidFastPair, nd, sizeof(nd)), s);
      }
      discoverable = !discoverable;
      s_bleLive = s;
      break;
    }

    default:
      return;
  }
  s_sent[s]++;
}

Signal currentBle() { return s_bleLive; }

void allStop() {
  NimBLEDevice::getAdvertising()->stop();
  s_bleLive = kSignalCount;
  WiFi.softAPdisconnect(true);
  WiFi.mode(WIFI_OFF);
  s_wifiUp = false;
}

uint32_t sentCount(Signal s) {
  return (s < kSignalCount) ? s_sent[s] : 0;
}

uint32_t txFailures() { return s_txFail; }

}  // namespace Emit
