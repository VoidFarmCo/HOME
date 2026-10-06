#include "chat_subghz.h"
#include "chat_core.h"   // CHAT_FRAME_MAX
#include "shared.h"
#include "SpiBus.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

/* Bounded CC1101 presence check (subghz.cpp). ELECHOUSE's Init() opens with an
 * unbounded `while (digitalRead(MISO));` that hangs with no module wired, so
 * init() is only called once this says a chip is there. */
bool subghzCc1101Present();

namespace SubghzChat {

static constexpr float CHAT_MHZ = 433.92f;
static bool s_up = false;

bool available() { return subghzCc1101Present(); }

bool init() {
  SpiBus::claim(SpiBus::Dev::Cc1101);   // own the bus: touch may have repointed it
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(1);          // packet mode (FIFO + packet handler)
  ELECHOUSE_cc1101.setModulation(0);      // 2-FSK
  ELECHOUSE_cc1101.setMHZ(CHAT_MHZ);
  ELECHOUSE_cc1101.setDRate(4.8);         // 4.8 kbps -- robust, plenty for text
  ELECHOUSE_cc1101.setSyncMode(2);        // 16/16 sync bits
  ELECHOUSE_cc1101.setSyncWord(0x48, 0x4D);  // "HM": the H.O.M.E net
  ELECHOUSE_cc1101.setCrc(1);             // drop corrupt packets in hardware
  ELECHOUSE_cc1101.setLengthConfig(1);    // variable length (length byte first)
  ELECHOUSE_cc1101.setPacketLength((byte)Chat::CHAT_FRAME_MAX);
  ELECHOUSE_cc1101.SetRx();
  s_up = true;
  return true;
}

void deinit() {
  if (!s_up) return;
  SpiBus::claim(SpiBus::Dev::Cc1101);
  ELECHOUSE_cc1101.setSidle();
  s_up = false;
}

bool send(const uint8_t* data, uint8_t len) {
  if (!s_up) return false;
  SpiBus::claim(SpiBus::Dev::Cc1101);
  ELECHOUSE_cc1101.SendData((byte*)data, (byte)len);
  ELECHOUSE_cc1101.SetRx();               // back to listening after the burst
  return true;
}

uint8_t poll(uint8_t* buf, uint8_t maxLen) {
  if (!s_up) return 0;
  SpiBus::claim(SpiBus::Dev::Cc1101);     // loop()'s touch read repoints the bus each tick
  if (!ELECHOUSE_cc1101.CheckReceiveFlag()) return 0;
  uint8_t raw[64];
  int len = (int)ELECHOUSE_cc1101.ReceiveData(raw);
  ELECHOUSE_cc1101.SetRx();
  if (len < 1 || !ELECHOUSE_cc1101.CheckCRC()) return 0;
  if (len > maxLen) len = maxLen;
  memcpy(buf, raw, len);
  return (uint8_t)len;
}

const char* label() { return "SubGHz"; }

}  // namespace SubghzChat
