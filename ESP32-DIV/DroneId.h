#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * DroneId — reading ASTM F3411 / ASD-STAN Broadcast Remote ID.
 *
 * A drone required to carry Remote ID shouts, in the clear, on radios this
 * board already listens to. There is no pairing, no key and no session: the
 * aircraft broadcasts its identity, its position and its operator's position
 * to anybody within range, because that is the entire point of the rule.
 *
 * This decodes it. It transmits nothing and connects to nothing, which puts
 * it in the same bracket as Spotter rather than the same bracket as the
 * jammers.
 *
 * ── The two transports this board can hear ──────────────────────────────────
 *
 * Wi-Fi Beacon    a vendor-specific information element, 0xDD, carrying the
 *                 ASD-STAN OUI FA:0B:BC and vendor type 0x0D. Spotter's
 *                 promiscuous handler already walks beacon IEs and already
 *                 reads vendor elements, so the frame arrives parsed.
 *
 * BLE 4 legacy    service data under 16-bit UUID 0xFFFA (ASTM International)
 *                 with application code 0x0D. The same shape FastPair reads
 *                 under 0xFE2C.
 *
 * Both then carry a one-byte message counter and one or more 25-byte
 * messages. That is the whole framing.
 *
 * ── What this board CANNOT hear, which matters ──────────────────────────────
 *
 * BLE 5 Long Range / extended advertising is the third transport, and a
 * classic ESP32 has no receiver for it. The WROOM-32 on this panel is BLE
 * 4.2. A drone broadcasting only on BT5 Coded PHY is silent here and there
 * is no way to tell that apart from no drone at all.
 *
 * Wi-Fi NAN is the fourth. Its service id is a hash, 88:69:19:9D:92:09, and
 * it rides in public action frames rather than beacons. Receivable in
 * principle; not done here.
 *
 * So a negative result means "nothing on Wi-Fi Beacon or BLE 4 legacy", and
 * the UI has to say that rather than "no drones".
 *
 * ── Where the numbers came from ─────────────────────────────────────────────
 *
 * Every constant and every scale factor below was taken from the
 * opendroneid-core-c reference implementation rather than from memory:
 * the transport constants from libopendroneid/wifi.c and transmitter-linux's
 * bluetooth.c, the message layouts and the decode arithmetic from
 * libopendroneid/opendroneid.{h,c}. tools/check_droneid.py transcribes the
 * arithmetic independently and checks it against built vectors.
 *
 * The scale factors are the part worth guarding. Getting altitude or speed
 * wrong does not fail loudly -- it produces a plausible number for a real
 * aircraft, which is worse than producing nothing.
 * ────────────────────────────────────────────────────────────────────────── */

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

namespace DroneId {

/* Transport framing. */
constexpr uint8_t  kWifiElementId   = 0xDD;   // IEEE80211_ELEMID_VENDOR
constexpr uint8_t  kWifiOui[3]      = {0xFA, 0x0B, 0xBC};   // ASD-STAN
constexpr uint8_t  kWifiOuiType     = 0x0D;
constexpr uint16_t kBleServiceUuid  = 0xFFFA; // ASTM International
constexpr uint8_t  kBleAppCode      = 0x0D;   // Open Drone ID

/* Messages. */
constexpr size_t  kMessageSize    = 25;
constexpr uint8_t kPackMaxMessages = 9;
constexpr uint8_t kProtocolVersion = 2;

enum MessageType : uint8_t {
  BasicId    = 0x0,
  Location   = 0x1,
  Auth       = 0x2,
  SelfId     = 0x3,
  System     = 0x4,
  OperatorId = 0x5,
  Packed     = 0xF,
};

/* Sentinels the standard defines for "this field is not being sent".
 * Printing 0 for an absent altitude would be a lie with a plausible face. */
constexpr uint16_t kInvalidTimestamp = 0xFFFF;
constexpr uint8_t  kInvalidSpeedH    = 0xFF;
constexpr int8_t   kInvalidSpeedV    = 63;
constexpr uint16_t kInvalidDirection = 361;

/* One aircraft, accumulated across the message types that describe it.
 * A single advertisement rarely carries everything: Basic ID names it,
 * Location moves it, System says where the operator is standing. */
struct Report {
  char    uasId[21];        // Basic ID, NUL-terminated
  uint8_t idType;           // 0 none, 1 serial, 2 CAA reg, 3 UTM UUID, 4 session
  uint8_t uaType;           // 0 none, 1 aeroplane, 2 helicopter/multirotor, ...

  bool    haveLocation;
  double  latitude;         // degrees
  double  longitude;
  float   altitudeGeo;      // metres, WGS-84
  float   height;           // metres above takeoff or ground
  float   speedHorizontal;  // m/s
  float   speedVertical;    // m/s
  float   direction;        // degrees true
  uint8_t status;           // 0 undeclared, 1 ground, 2 airborne, 3 emergency
  float   timestamp;        // seconds into the hour, <0 when not sent

  bool    haveOperator;
  double  operatorLatitude;
  double  operatorLongitude;

  char    operatorId[21];   // Operator ID, NUL-terminated
  bool    haveOperatorId;
};

/* Decode one 25-byte message into `out`, merging with whatever is already
 * there. Returns false on a message this does not understand, which
 * includes a protocol version it was not written against. */
bool decodeMessage(const uint8_t* msg, size_t len, Report& out);

/* Decode a payload that may be a single message or a message pack, i.e.
 * everything after the transport's message counter. Returns the number of
 * messages successfully decoded. */
int decodePayload(const uint8_t* payload, size_t len, Report& out);

/* Transport entry points. Each validates its own framing and hands the
 * remainder to decodePayload().
 *
 * `ie` is a whole vendor element including its id and length bytes.
 * `serviceData` is the bytes after the 16-bit UUID in a BLE service-data AD
 * structure, i.e. starting at the application code. */
int fromWifiIe(const uint8_t* ie, size_t len, Report& out);
int fromBleServiceData(const uint8_t* serviceData, size_t len, Report& out);

}  // namespace DroneId
