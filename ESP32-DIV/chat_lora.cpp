#include "chat_lora.h"
#include "mesh.h"
#include "Sx1262.h"
#include <Arduino.h>

namespace LoRaChat {

static constexpr uint32_t LORA_FREQ_HZ = 915000000;   // US ISM; match your region
static bool s_up = false;
static bool s_probed = false;
static bool s_present = false;

/* One cached bring-up so the channel picker can self-report without a session. */
bool available() {
  if (!s_probed) {
    s_present = Sx1262::begin(LORA_FREQ_HZ);
    s_probed = true;
  }
  return s_present;
}

bool init() {
  if (!Sx1262::begin(LORA_FREQ_HZ)) { s_up = false; return false; }
  Mesh::begin((uint16_t)(ESP.getEfuseMac() & 0xFFFF));
  Sx1262::startReceive();
  s_up = true;
  s_probed = true;
  s_present = true;
  return true;
}

void deinit() { s_up = false; /* radio left idle by the next begin(); no std-by op needed */ }

bool send(const uint8_t* data, uint8_t len) {
  if (!s_up) return false;
  uint8_t pkt[Mesh::MESH_MAX_PACKET];
  const int n = Mesh::wrap(data, len, pkt, (int)sizeof(pkt));
  if (n <= 0) return false;
  const bool ok = Sx1262::send(pkt, (uint8_t)n);
  Sx1262::startReceive();                 // back to listening
  return ok;
}

uint8_t poll(uint8_t* buf, uint8_t maxLen) {
  if (!s_up) return 0;
  uint8_t pkt[Mesh::MESH_MAX_PACKET];
  int16_t rssi = 0;
  const uint8_t n = Sx1262::receive(pkt, (uint8_t)sizeof(pkt), &rssi);
  if (n == 0) return 0;

  uint8_t relay[Mesh::MESH_MAX_PACKET];
  int relayLen = 0;
  const int plen = Mesh::unwrap(pkt, (int)n, buf, (int)maxLen,
                                relay, &relayLen, (int)sizeof(relay));
  if (relayLen > 0) {                     // forward the flood before showing it
    Sx1262::send(relay, (uint8_t)relayLen);
    Sx1262::startReceive();
  }
  return (plen > 0) ? (uint8_t)plen : 0;
}

const char* label() { return "LoRa Mesh"; }

}  // namespace LoRaChat
