#include "Eapol.h"

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

}  // namespace Eapol
