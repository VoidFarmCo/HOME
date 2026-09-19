#include "Eapol.h"

#include <string.h>

namespace Eapol {

namespace {

/* Frame Control, octet 0: protocol version in bits 0-1, type in 2-3,
 * subtype in 4-7. */
constexpr uint8_t kTypeData = 0x02;

/* Subtype bits that matter here. Bit 3 marks the QoS variants; bit 2 marks
 * the Null variants, which carry no body at all. */
constexpr uint8_t kSubtypeQos  = 0x08;
constexpr uint8_t kSubtypeNull = 0x04;

/* Frame Control, octet 1. */
constexpr uint8_t kToDs      = 0x01;
constexpr uint8_t kFromDs    = 0x02;
constexpr uint8_t kProtected = 0x40;
constexpr uint8_t kOrder     = 0x80;

/* LLC/SNAP: DSAP, SSAP, control, three-byte OUI, then the ethertype. Eight
 * bytes, with the ethertype in the last two. */
constexpr uint8_t kSnapDsap = 0xAA;
constexpr uint8_t kSnapSsap = 0xAA;
constexpr uint8_t kSnapCtrl = 0x03;
constexpr int     kSnapLen  = 8;
constexpr int     kSnapEthertypeOffset = 6;

constexpr uint8_t kEapolEthertypeHi = 0x88;
constexpr uint8_t kEapolEthertypeLo = 0x8E;

/* 802.1X header: version, packet type, two-byte body length. Then the
 * EAPOL-Key body begins with a descriptor type and the Key Information
 * field, so Key Information sits five bytes into the payload. */
constexpr int     kPacketTypeOffset = 1;
constexpr uint8_t kPacketTypeKey    = 0x03;
constexpr int     kKeyInfoOffset    = 5;

/* Key Information bits that separate the four messages.
 *
 *          Key Ack   Key MIC   Secure
 *   M1        yes       no       no
 *   M2        no        yes      no
 *   M3        yes       yes      yes
 *   M4        no        yes      yes
 *
 * This mapping is ESP32Marauder's, which is MIT licensed, Copyright (c)
 * 2020 Just Call Me Koko. It is the one part of that implementation worth
 * taking as it stands. */
constexpr uint16_t kKeyType = 1u << 3;   // set = pairwise, clear = group
constexpr uint16_t kKeyAck  = 1u << 7;
constexpr uint16_t kKeyMic  = 1u << 8;
constexpr uint16_t kSecure  = 1u << 9;

/* Address fields in an 802.11 header. */
constexpr int kAddr1 = 4;
constexpr int kAddr2 = 10;
constexpr int kAddr3 = 16;
constexpr int kAddrBytes = 6;

Handshake s_handshakes[kMaxHandshakes];
int       s_handshakeCount = 0;

bool     s_assistArmed  = false;
int      s_assistBursts = 0;
uint32_t s_assistLastMs = 0;

}  // namespace

int headerLength(const uint8_t* frame, uint16_t len) {
  if (frame == nullptr || len < 2) {
    return -1;                          // no frame control to read
  }

  const uint8_t type = (uint8_t)((frame[0] >> 2) & 0x03);
  if (type != kTypeData) {
    return -1;                          // management and control carry none
  }

  const uint8_t subtype = (uint8_t)((frame[0] >> 4) & 0x0F);
  if (subtype & kSubtypeNull) {
    return -1;                          // Null and QoS Null: header only
  }

  const uint8_t flags = frame[1];

  int hdr = 24;
  if ((flags & (kToDs | kFromDs)) == (kToDs | kFromDs)) {
    hdr += 6;                           // Address 4, on a WDS frame
  }
  if (subtype & kSubtypeQos) {
    hdr += 2;                           // QoS Control
    if (flags & kOrder) {
      hdr += 4;                         // HT Control, QoS frames only
    }
  }
  return hdr;
}

int findPayload(const uint8_t* frame, uint16_t len) {
  const int hdr = headerLength(frame, len);
  if (hdr < 0) {
    return -1;
  }

  /* Protected frames carry ciphertext plus a key header where the LLC would
   * be, so there is nothing to match and nothing to read. */
  if (frame[1] & kProtected) {
    return -1;
  }

  /* Bound before reading. hdr is at most 36 and len at most 65535, so the
   * sum cannot wrap, but it is done in a wider type anyway rather than
   * relying on that staying true. */
  if ((uint32_t)hdr + (uint32_t)kSnapLen > (uint32_t)len) {
    return -1;
  }

  if (frame[hdr] != kSnapDsap ||
      frame[hdr + 1] != kSnapSsap ||
      frame[hdr + 2] != kSnapCtrl) {
    return -1;                          // not LLC/SNAP encapsulation
  }

  if (frame[hdr + kSnapEthertypeOffset] != kEapolEthertypeHi ||
      frame[hdr + kSnapEthertypeOffset + 1] != kEapolEthertypeLo) {
    return -1;                          // SNAP, but not 802.1X
  }

  return hdr + kSnapLen;
}

Msg classify(const uint8_t* frame, uint16_t len, int eapolOffset) {
  if (frame == nullptr || eapolOffset < 0) {
    return Msg::None;
  }

  /* Key Information is two bytes at +5, so +6 has to be readable. */
  if ((uint32_t)eapolOffset + (uint32_t)kKeyInfoOffset + 1u >= (uint32_t)len) {
    return Msg::None;
  }

  if (frame[eapolOffset + kPacketTypeOffset] != kPacketTypeKey) {
    return Msg::None;                   // EAP, Start, Logoff: no key info
  }

  const uint16_t keyInfo =
      (uint16_t)((uint16_t)frame[eapolOffset + kKeyInfoOffset] << 8 |
                 (uint16_t)frame[eapolOffset + kKeyInfoOffset + 1]);

  /* Pairwise only. A group rekey carries Key Ack, Key MIC and Secure
   * together, which is exactly the M3 pattern, so without this a rekey on an
   * idle network would look like two thirds of a handshake. Marauder does not
   * make this distinction. */
  if ((keyInfo & kKeyType) == 0) {
    return Msg::None;
  }

  const bool ack    = (keyInfo & kKeyAck) != 0;
  const bool mic    = (keyInfo & kKeyMic) != 0;
  const bool secure = (keyInfo & kSecure) != 0;

  if (ack && !mic && !secure) {
    return Msg::M1;
  }
  if (!ack && mic && !secure) {
    return Msg::M2;
  }
  if (ack && mic && secure) {
    return Msg::M3;
  }
  if (!ack && mic && secure) {
    return Msg::M4;
  }
  return Msg::None;                     // a combination with no name here
}

bool addresses(const uint8_t* frame, uint16_t len,
               const uint8_t** bssid, const uint8_t** station) {
  if (frame == nullptr || bssid == nullptr || station == nullptr) {
    return false;
  }
  if (len < (uint16_t)(kAddr3 + kAddrBytes)) {
    return false;                       // not enough frame for three addresses
  }

  const uint8_t flags = frame[1];
  const bool toDs   = (flags & kToDs) != 0;
  const bool fromDs = (flags & kFromDs) != 0;

  if (toDs && fromDs) {
    return false;                       // WDS: four addresses, no one BSSID
  }

  if (fromDs) {                         // access point to station
    *bssid   = frame + kAddr2;
    *station = frame + kAddr1;
  } else if (toDs) {                    // station to access point
    *bssid   = frame + kAddr1;
    *station = frame + kAddr2;
  } else {                              // neither: ad-hoc
    *bssid   = frame + kAddr3;
    *station = frame + kAddr2;
  }
  return true;
}

uint8_t maskOf(Msg m) {
  switch (m) {
    case Msg::M1: return 0x01;
    case Msg::M2: return 0x02;
    case Msg::M3: return 0x04;
    case Msg::M4: return 0x08;
    default:      return 0x00;
  }
}

bool usable(const Handshake& h) {
  return (h.seen & (0x02 | 0x04)) == (0x02 | 0x04);
}

Msg observe(const uint8_t* frame, uint16_t len, uint32_t nowMs) {
  const int off = findPayload(frame, len);
  if (off < 0) {
    return Msg::None;
  }
  const Msg msg = classify(frame, len, off);
  if (msg == Msg::None) {
    return Msg::None;
  }

  const uint8_t* bssid = nullptr;
  const uint8_t* station = nullptr;
  if (!addresses(frame, len, &bssid, &station)) {
    return msg;                         // classified, but nowhere to file it
  }

  for (int i = 0; i < s_handshakeCount; i++) {
    if (memcmp(s_handshakes[i].bssid, bssid, kAddrBytes) == 0) {
      /* A different station talking to the same AP starts the count again:
       * messages from two clients do not add up to one handshake. */
      if (memcmp(s_handshakes[i].station, station, kAddrBytes) != 0) {
        memcpy(s_handshakes[i].station, station, kAddrBytes);
        s_handshakes[i].seen = 0;
        s_handshakes[i].firstMs = nowMs;
      }
      s_handshakes[i].seen |= maskOf(msg);
      s_handshakes[i].lastMs = nowMs;
      return msg;
    }
  }

  if (s_handshakeCount >= kMaxHandshakes) {
    return msg;                         // full: drop rather than evict
  }

  Handshake& h = s_handshakes[s_handshakeCount++];
  memset(&h, 0, sizeof(h));
  memcpy(h.bssid, bssid, kAddrBytes);
  memcpy(h.station, station, kAddrBytes);
  h.seen = maskOf(msg);
  h.firstMs = nowMs;
  h.lastMs = nowMs;
  return msg;
}

int handshakeCount() {
  return s_handshakeCount;
}

const Handshake* handshakeAt(int i) {
  if (i < 0 || i >= s_handshakeCount) {
    return nullptr;
  }
  return &s_handshakes[i];
}

void resetHandshakes() {
  s_handshakeCount = 0;
  memset(s_handshakes, 0, sizeof(s_handshakes));
  s_assistArmed = false;
  s_assistBursts = 0;
  s_assistLastMs = 0;
}

void assistArm(bool on) {
  s_assistArmed = on;
  s_assistBursts = 0;
  s_assistLastMs = 0;
}

bool assistArmed() {
  return s_assistArmed;
}

int assistBursts() {
  return s_assistBursts;
}

int assistDue(uint32_t nowMs) {
  if (!s_assistArmed) {
    return -1;
  }

  /* Permanent stop: the cap is reached. Disarm rather than keep saying no,
   * so the UI shows it is finished and re-arming is deliberate. */
  if (s_assistBursts >= kAssistMaxBursts) {
    s_assistArmed = false;
    return -1;
  }

  /* First burst goes immediately; after that, one per interval. */
  if (s_assistBursts > 0 &&
      (uint32_t)(nowMs - s_assistLastMs) < kAssistIntervalMs) {
    return -1;
  }

  /* Only a network that has shown part of a handshake and not the rest. A
   * row exists only once an EAPOL frame from it has been received, so this
   * cannot be aimed somewhere nothing was happening. */
  int target = -1;
  for (int i = 0; i < s_handshakeCount; i++) {
    if (s_handshakes[i].seen != 0 && !usable(s_handshakes[i])) {
      target = i;
      break;
    }
  }

  if (target < 0) {
    /* Nothing incomplete. If nothing is tracked at all there is nothing to
     * do yet; if everything tracked is complete, the job is done and it
     * disarms. */
    if (s_handshakeCount > 0) {
      s_assistArmed = false;
    }
    return -1;
  }

  s_assistLastMs = nowMs;
  s_assistBursts++;
  return target;
}

}  // namespace Eapol
