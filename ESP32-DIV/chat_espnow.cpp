#include "chat_espnow.h"
#include "chat_core.h"   // CHAT_FRAME_MAX
#include <Arduino.h>
#include <stdlib.h>
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>

namespace EspNowChat {

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static bool s_up = false;

/* Small receive ring filled from the ESP-NOW callback (runs in the Wi-Fi task),
 * drained by poll() on the UI task. Kept tiny -- chat is low-rate. */
static constexpr int RX_SLOTS = 3;
struct RxFrame { uint8_t data[Chat::CHAT_FRAME_MAX]; volatile uint8_t len; };
static RxFrame* s_rx = nullptr;   // heap: allocated for a chat session only
static volatile int s_rxHead = 0;   // next write
static volatile int s_rxTail = 0;   // next read

static void onRecv(const uint8_t* /*mac*/, const uint8_t* data, int len) {
  if (len < 1 || !s_rx) return;
  if (len > Chat::CHAT_FRAME_MAX) len = Chat::CHAT_FRAME_MAX;
  const int next = (s_rxHead + 1) % RX_SLOTS;
  if (next == s_rxTail) return;       // ring full: drop (low-rate, acceptable)
  memcpy(s_rx[s_rxHead].data, data, len);
  s_rx[s_rxHead].len = (uint8_t)len;
  s_rxHead = next;
}

bool available() { return true; }     // the ESP32's own radio is always there

bool init() {
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();                  // broadcast only; never joins an AP
  if (!s_rx) s_rx = (RxFrame*)calloc(RX_SLOTS, sizeof(RxFrame));
  if (!s_rx) return false;
  if (esp_now_init() != ESP_OK) { s_up = false; return false; }
  esp_now_register_recv_cb(onRecv);
  esp_now_peer_info_t peer;
  memset(&peer, 0, sizeof(peer));
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;                   // current channel
  peer.encrypt = false;               // broadcast peers cannot be encrypted
  esp_now_add_peer(&peer);
  s_rxHead = s_rxTail = 0;
  s_up = true;
  return true;
}

void deinit() {
  if (!s_up) return;
  esp_now_unregister_recv_cb();
  esp_now_del_peer(BCAST);
  esp_now_deinit();
  if (s_rx) { free(s_rx); s_rx = nullptr; }
  s_up = false;
}

bool send(const uint8_t* data, uint8_t len) {
  if (!s_up) return false;
  return esp_now_send(BCAST, data, len) == ESP_OK;
}

uint8_t poll(uint8_t* buf, uint8_t maxLen) {
  if (!s_up || s_rxTail == s_rxHead) return 0;
  uint8_t len = s_rx[s_rxTail].len;
  if (len > maxLen) len = maxLen;
  memcpy(buf, s_rx[s_rxTail].data, len);
  s_rxTail = (s_rxTail + 1) % RX_SLOTS;
  return len;
}

const char* label() { return "ESP-NOW"; }

}  // namespace EspNowChat
