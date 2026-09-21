#include "DroneId.h"

#include <string.h>

namespace DroneId {
namespace {

/* Scale factors, from libopendroneid/opendroneid.c. Named rather than
 * inlined so tools/check_droneid.py can be read against this file line for
 * line, which is the only way a wrong 0.25 gets noticed. */
constexpr double kLatLonMult = 10000000.0;  // LATLON_MULT
constexpr float  kAltDiv     = 0.5f;        // ALT_DIV
constexpr float  kAltAdder   = 1000.0f;     // ALT_ADDER
constexpr float  kSpeedDiv0  = 0.25f;       // SPEED_DIV[0]
constexpr float  kSpeedDiv1  = 0.75f;       // SPEED_DIV[1]
constexpr float  kVspeedDiv  = 0.5f;        // VSPEED_DIV

/* Little-endian reads. The messages are packed structs on a little-endian
 * transmitter, and the ESP32 is little-endian too, but casting a struct over
 * a frame off the air is how alignment faults and bitfield-order surprises
 * happen. Reading byte by byte costs nothing here and travels. */
uint16_t rd16(const uint8_t* p) {
  return (uint16_t)((uint16_t)p[0] | ((uint16_t)p[1] << 8));
}

uint32_t rd32(const uint8_t* p) {
  return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
         ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

int32_t rds32(const uint8_t* p) {
  return (int32_t)rd32(p);
}

double decodeLatLon(int32_t enc) {
  return (double)enc / kLatLonMult;
}

float decodeAltitude(uint16_t enc) {
  return (float)enc * kAltDiv - kAltAdder;
}

float decodeDirection(uint8_t enc, uint8_t ewDirection) {
  return ewDirection ? (float)enc + 180.0f : (float)enc;
}

float decodeSpeedHorizontal(uint8_t enc, uint8_t mult) {
  if (mult) {
    return ((float)enc * kSpeedDiv1) + (255.0f * kSpeedDiv0);
  }
  return (float)enc * kSpeedDiv0;
}

float decodeSpeedVertical(int8_t enc) {
  return (float)enc * kVspeedDiv;
}

/* The standard's text fields are fixed-width and NOT guaranteed to be
 * NUL-terminated -- a 20-character serial fills the field exactly. Copying
 * with strncpy and trusting it would run the print off the end of the
 * struct. */
void copyFixedText(char* dst, size_t dstSz, const uint8_t* src, size_t n) {
  const size_t lim = (n < dstSz - 1) ? n : dstSz - 1;
  size_t i = 0;
  for (; i < lim; i++) {
    const uint8_t c = src[i];
    if (c == 0) break;
    /* Anything outside printable ASCII is replaced rather than passed to a
     * font that has no glyph for it. The field is attacker-controlled. */
    dst[i] = (c >= 0x20 && c < 0x7F) ? (char)c : '?';
  }
  dst[i] = '\0';
}

}  // namespace

bool decodeMessage(const uint8_t* msg, size_t len, Report& out) {
  if (msg == nullptr || len < kMessageSize) return false;

  const uint8_t type    = (uint8_t)(msg[0] >> 4);
  const uint8_t version = (uint8_t)(msg[0] & 0x0F);

  /* A version this was not written against is refused rather than decoded
   * hopefully. Field positions have moved between versions before. */
  if (version > kProtocolVersion) return false;

  switch (type) {
    case BasicId: {
      out.idType = (uint8_t)(msg[1] >> 4);
      out.uaType = (uint8_t)(msg[1] & 0x0F);
      copyFixedText(out.uasId, sizeof(out.uasId), msg + 2, 20);
      return true;
    }

    case Location: {
      const uint8_t flags = msg[1];
      const uint8_t speedMult  = (uint8_t)(flags & 0x01);
      const uint8_t ewDir      = (uint8_t)((flags >> 1) & 0x01);
      out.status = (uint8_t)((flags >> 4) & 0x0F);

      const uint8_t dirEnc = msg[2];
      out.direction = (dirEnc == 255) ? -1.0f : decodeDirection(dirEnc, ewDir);

      out.speedHorizontal = (msg[3] == kInvalidSpeedH)
                              ? -1.0f
                              : decodeSpeedHorizontal(msg[3], speedMult);
      const int8_t sv = (int8_t)msg[4];
      out.speedVertical = (sv == kInvalidSpeedV) ? 0.0f : decodeSpeedVertical(sv);

      out.latitude    = decodeLatLon(rds32(msg + 5));
      out.longitude   = decodeLatLon(rds32(msg + 9));
      out.altitudeGeo = decodeAltitude(rd16(msg + 15));
      out.height      = decodeAltitude(rd16(msg + 17));

      const uint16_t ts = rd16(msg + 21);
      out.timestamp = (ts == kInvalidTimestamp) ? -1.0f : (float)ts / 10.0f;

      /* 0,0 is in the Gulf of Guinea and is what an unset field looks like.
       * Treating it as a fix puts a drone off the coast of Africa on the
       * screen, which is worse than showing no position. */
      out.haveLocation = !(out.latitude == 0.0 && out.longitude == 0.0);
      return true;
    }

    case System: {
      out.operatorLatitude  = decodeLatLon(rds32(msg + 2));
      out.operatorLongitude = decodeLatLon(rds32(msg + 6));
      out.haveOperator =
          !(out.operatorLatitude == 0.0 && out.operatorLongitude == 0.0);
      return true;
    }

    case OperatorId: {
      copyFixedText(out.operatorId, sizeof(out.operatorId), msg + 2, 20);
      out.haveOperatorId = (out.operatorId[0] != '\0');
      return true;
    }

    /* Known, carries nothing this screen shows. Reported as understood so a
     * caller counting decodes does not treat a valid frame as junk. */
    case Auth:
    case SelfId:
      return true;

    default:
      return false;
  }
}

int decodePayload(const uint8_t* payload, size_t len, Report& out) {
  if (payload == nullptr || len < 1) return 0;

  const uint8_t type = (uint8_t)(payload[0] >> 4);

  if (type != Packed) {
    return decodeMessage(payload, len, out) ? 1 : 0;
  }

  /* A message pack states both its element size and its count, and both are
   * off the air. A pack claiming nine messages inside a frame holding two
   * is the obvious way to walk this off the end of the buffer. */
  if (len < 3) return 0;
  const uint8_t singleSize = payload[1];
  const uint8_t count      = payload[2];
  if (singleSize != kMessageSize) return 0;
  if (count == 0 || count > kPackMaxMessages) return 0;
  if ((size_t)count * kMessageSize + 3 > len) return 0;

  int decoded = 0;
  for (uint8_t i = 0; i < count; i++) {
    const uint8_t* m = payload + 3 + ((size_t)i * kMessageSize);
    if (decodeMessage(m, kMessageSize, out)) decoded++;
  }
  return decoded;
}

int fromWifiIe(const uint8_t* ie, size_t len, Report& out) {
  /* id, length, 3-byte OUI, vendor type, message counter = 7 before payload */
  if (ie == nullptr || len < 7) return 0;
  if (ie[0] != kWifiElementId) return 0;

  const size_t declared = ie[1];
  if (declared + 2 > len) return 0;          // element longer than the frame
  if (declared < 5) return 0;                // no room for OUI + type + counter

  if (memcmp(ie + 2, kWifiOui, sizeof(kWifiOui)) != 0) return 0;
  if (ie[5] != kWifiOuiType) return 0;

  /* ie[6] is the message counter, which this does not use: it exists to let
   * a receiver notice gaps, and nothing here is counting. */
  return decodePayload(ie + 7, declared - 5, out);
}

int fromBleServiceData(const uint8_t* serviceData, size_t len, Report& out) {
  /* app code, message counter, then the payload */
  if (serviceData == nullptr || len < 3) return 0;
  if (serviceData[0] != kBleAppCode) return 0;
  return decodePayload(serviceData + 2, len - 2, out);
}

}  // namespace DroneId
