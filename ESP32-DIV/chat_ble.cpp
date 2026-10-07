#include "chat_ble.h"
#include "chat_core.h"   // CHAT_FRAME_MAX
#include <Arduino.h>
#include <string>
#include <string.h>
#include <stdlib.h>
#include <NimBLEDevice.h>

namespace BleChat {

/* A private company id so we only ever hear our own chat adverts, not every
 * beacon in range. Not an assigned Bluetooth SIG id -- it is a local marker. */
static constexpr uint16_t HOME_BLE_COMPANY = 0x484D;   // 'M','H' little-endian

/* A legacy advert is 31 bytes: 3 (flags) + 1 len + 1 type(0xFF) + 2 company id
 * leaves 24 for the chat frame. Longer frames are clipped to this on BLE. */
static constexpr uint8_t BLE_PAYLOAD_MAX = 24;

/* RX ring filled from the scan callback (BLE task), drained by poll() (UI task),
 * same shape as the ESP-NOW transport. Heap: a chat session only. */
static constexpr int RX_SLOTS = 3;
struct RxFrame { uint8_t data[Chat::CHAT_FRAME_MAX]; volatile uint8_t len; };
static RxFrame* s_rx = nullptr;
static volatile int s_rxHead = 0;
static volatile int s_rxTail = 0;
static bool s_up = false;

/* Drop an advert that is byte-identical to the last one within this window: a
 * single send pulses the advert for a fraction of a second and the scanner, set
 * to report repeats, would otherwise log the same message several times. */
static uint8_t  s_lastFrame[Chat::CHAT_FRAME_MAX];
static uint8_t  s_lastLen = 0;
static uint32_t s_lastMs  = 0;
static constexpr uint32_t DEDUPE_MS = 1500;

class RxCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* dev) override {
    if (!s_rx || !dev->haveManufacturerData()) return;
    const std::string md = dev->getManufacturerData();
    if (md.size() < 3) return;
    const uint16_t company = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
    if (company != HOME_BLE_COMPANY) return;

    int n = (int)md.size() - 2;                 // frame is everything past the id
    if (n < 1) return;
    if (n > Chat::CHAT_FRAME_MAX) n = Chat::CHAT_FRAME_MAX;
    const uint8_t* frame = (const uint8_t*)md.data() + 2;

    const uint32_t now = millis();
    if (s_lastLen == n && (now - s_lastMs) < DEDUPE_MS &&
        memcmp(s_lastFrame, frame, n) == 0) {
      return;                                   // same message still echoing
    }
    memcpy(s_lastFrame, frame, n);
    s_lastLen = (uint8_t)n;
    s_lastMs  = now;

    const int next = (s_rxHead + 1) % RX_SLOTS;
    if (next == s_rxTail) return;               // ring full: drop (low-rate)
    memcpy(s_rx[s_rxHead].data, frame, n);
    s_rx[s_rxHead].len = (uint8_t)n;
    s_rxHead = next;
  }
};
static RxCallbacks s_cb;
static BLEScan* s_scan = nullptr;

bool available() { return true; }               // the ESP32's own BLE radio

static void startScan() {
  if (!s_scan) s_scan = BLEDevice::getScan();
  s_scan->setAdvertisedDeviceCallbacks(&s_cb, true);  // true: report repeats
  s_scan->setActiveScan(false);                 // passive: only listen
  s_scan->setInterval(160);                     // 160 * 0.625 ms = 100 ms
  s_scan->setWindow(160);
  s_scan->start(0, nullptr, false);             // 0 == scan until stopped
}

bool init() {
  if (!s_rx) s_rx = (RxFrame*)calloc(RX_SLOTS, sizeof(RxFrame));
  if (!s_rx) return false;
  if (!BLEDevice::getInitialized()) BLEDevice::init("");
  s_rxHead = s_rxTail = 0;
  s_lastLen = 0;
  startScan();
  s_up = true;
  return true;
}

void deinit() {
  if (!s_up) return;
  if (s_scan) {
    s_scan->stop();
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
  }
  BLEDevice::getAdvertising()->stop();
  if (s_rx) { free(s_rx); s_rx = nullptr; }
  s_up = false;
}

bool send(const uint8_t* data, uint8_t len) {
  if (!s_up) return false;
  uint8_t n = (len > BLE_PAYLOAD_MAX) ? BLE_PAYLOAD_MAX : len;   // BLE: short only

  uint8_t raw[31];
  int i = 0;
  raw[i++] = 0x02; raw[i++] = 0x01; raw[i++] = 0x06;             // flags AD
  raw[i++] = (uint8_t)(1 + 2 + n);                              // manuf AD length
  raw[i++] = 0xFF;                                              // manuf-specific
  raw[i++] = (uint8_t)(HOME_BLE_COMPANY & 0xFF);
  raw[i++] = (uint8_t)(HOME_BLE_COMPANY >> 8);
  memcpy(raw + i, data, n); i += n;

  BLEAdvertisementData advData;
  advData.addData(std::string((char*)raw, i));

  /* Stop scanning while the advert pulses, then resume -- avoids leaning on
   * concurrent scan+advertise, and a single message is low-rate anyway. */
  if (s_scan) s_scan->stop();
  BLEAdvertising* adv = BLEDevice::getAdvertising();
  adv->stop();
  adv->setAdvertisementData(advData);
  adv->setAdvertisementType(BLE_GAP_CONN_MODE_NON);             // broadcast only
  adv->start();
  delay(220);                                                  // long enough to be heard
  adv->stop();
  startScan();
  return true;
}

uint8_t poll(uint8_t* buf, uint8_t maxLen) {
  if (!s_up || s_rxTail == s_rxHead) return 0;
  uint8_t len = s_rx[s_rxTail].len;
  if (len > maxLen) len = maxLen;
  memcpy(buf, s_rx[s_rxTail].data, len);
  s_rxTail = (s_rxTail + 1) % RX_SLOTS;
  return len;
}

const char* label() { return "BLE"; }

}  // namespace BleChat
