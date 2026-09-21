#include "DroneScan.h"

#include "DroneId.h"
#include "config.h"
#include "shared.h"
#include "utils.h"
#include "icon.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>

namespace DroneScan {
namespace {

constexpr uint8_t  kChanFirst = 1;
constexpr uint8_t  kChanLast  = 13;
constexpr uint32_t kHopMs     = 300;
constexpr uint32_t kRedrawMs  = 250;
constexpr uint16_t kBleWindowMs = 2000;
constexpr uint8_t  kMaxCraft  = 6;

/* One aircraft on screen. The Remote ID report plus what the radio layer
 * knows about it, which the report itself never carries. */
struct Craft {
  DroneId::Report rep;
  uint8_t  srcMac[6];
  int8_t   rssi;
  uint32_t firstMs;
  uint32_t lastMs;
  uint16_t frames;
  bool     viaBle;
  bool     used;
};

Craft   s_craft[kMaxCraft];
uint8_t s_craftCount = 0;
uint8_t s_chan       = kChanFirst;
uint32_t s_frames    = 0;      // Remote ID carriers seen, not total frames
bool    s_running    = false;
bool    s_dirty      = true;
uint32_t s_lastHop   = 0;
uint32_t s_lastDraw  = 0;
BLEScan* s_scan      = nullptr;

/* ── the queue between the promiscuous callback and the loop ──────────────
 *
 * The callback runs in the Wi-Fi task and is the wrong place to do float
 * arithmetic on an attacker-controlled frame. It copies the candidate
 * element out and returns; decodePayload runs in loop(). Spotter's capture
 * path is built the same way for the same reason. */
constexpr uint8_t kQueueSlots = 4;
constexpr uint8_t kQueueBytes = 64;

struct Slot {
  uint8_t  bytes[kQueueBytes];
  uint8_t  len;
  uint8_t  mac[6];
  int8_t   rssi;
};

volatile uint8_t s_qHead = 0;
volatile uint8_t s_qTail = 0;
Slot s_queue[kQueueSlots];
volatile uint32_t s_qDropped = 0;

/* What each line currently says, so only the lines that changed repaint.
 * Five slots per row: four lines down the left, plus the radio/RSSI tag that
 * shares the first line's band. See uiShowLine. */
constexpr uint8_t kLinesPerRow = 5;
char s_shownRow[kMaxCraft][kLinesPerRow][64];
char s_shownHdr[2][64];

void forgetDrawn() {
  memset(s_shownRow, 0, sizeof(s_shownRow));
  memset(s_shownHdr, 0, sizeof(s_shownHdr));
}

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

/* Find the row for this aircraft, or claim one.
 *
 * Keyed on the UAS ID when there is one, because that is the thing that
 * identifies the aircraft; the source address does not, and on BLE it is
 * usually random and rotates. Before a Basic ID message arrives there is no
 * ID to key on, so the address stands in and the row is adopted by the ID
 * as soon as one turns up. */
Craft* findOrAdd(const DroneId::Report& rep, const uint8_t* mac, bool viaBle) {
  if (rep.uasId[0] != '\0') {
    for (uint8_t i = 0; i < s_craftCount; i++) {
      if (s_craft[i].used && strcmp(s_craft[i].rep.uasId, rep.uasId) == 0) {
        return &s_craft[i];
      }
    }
  }
  for (uint8_t i = 0; i < s_craftCount; i++) {
    if (s_craft[i].used && s_craft[i].rep.uasId[0] == '\0' &&
        memcmp(s_craft[i].srcMac, mac, 6) == 0) {
      return &s_craft[i];
    }
  }
  if (s_craftCount < kMaxCraft) {
    Craft* c = &s_craft[s_craftCount++];
    memset(c, 0, sizeof(*c));
    c->used = true;
    c->firstMs = millis();
    c->rssi = -127;
    c->viaBle = viaBle;
    memcpy(c->srcMac, mac, 6);
    return c;
  }
  /* Full. Replace the one heard from longest ago rather than dropping the
   * new aircraft: the interesting one is usually the one that just arrived. */
  Craft* oldest = &s_craft[0];
  for (uint8_t i = 1; i < s_craftCount; i++) {
    if (s_craft[i].lastMs < oldest->lastMs) oldest = &s_craft[i];
  }
  memset(oldest, 0, sizeof(*oldest));
  oldest->used = true;
  oldest->firstMs = millis();
  oldest->rssi = -127;
  oldest->viaBle = viaBle;
  memcpy(oldest->srcMac, mac, 6);
  return oldest;
}

void ingest(const uint8_t* payload, size_t len, const uint8_t* mac,
            int8_t rssi, bool viaBle) {
  /* Decode into a scratch report first. A frame that turns out to carry
   * nothing we understand must not create a row. */
  DroneId::Report scratch;
  memset(&scratch, 0, sizeof(scratch));
  scratch.timestamp = -1.0f;

  const int n = viaBle ? DroneId::fromBleServiceData(payload, len, scratch)
                       : DroneId::fromWifiIe(payload, len, scratch);
  if (n <= 0) return;

  s_frames++;
  Craft* c = findOrAdd(scratch, mac, viaBle);

  /* Merge: a Location message carries no ID and an ID message carries no
   * position, so each one only overwrites what it actually said. */
  if (scratch.uasId[0] != '\0') {
    memcpy(c->rep.uasId, scratch.uasId, sizeof(c->rep.uasId));
    c->rep.idType = scratch.idType;
    c->rep.uaType = scratch.uaType;
  }
  if (scratch.haveLocation) {
    c->rep.haveLocation    = true;
    c->rep.latitude        = scratch.latitude;
    c->rep.longitude       = scratch.longitude;
    c->rep.altitudeGeo     = scratch.altitudeGeo;
    c->rep.height          = scratch.height;
    c->rep.speedHorizontal = scratch.speedHorizontal;
    c->rep.speedVertical   = scratch.speedVertical;
    c->rep.direction       = scratch.direction;
    c->rep.status          = scratch.status;
    c->rep.timestamp       = scratch.timestamp;
  }
  if (scratch.haveOperator) {
    c->rep.haveOperator      = true;
    c->rep.operatorLatitude  = scratch.operatorLatitude;
    c->rep.operatorLongitude = scratch.operatorLongitude;
  }
  if (scratch.haveOperatorId) {
    c->rep.haveOperatorId = true;
    memcpy(c->rep.operatorId, scratch.operatorId, sizeof(c->rep.operatorId));
  }

  c->lastMs = millis();
  c->frames++;
  c->rssi = rssi;
  s_dirty = true;
}

/* ── Wi-Fi ───────────────────────────────────────────────────────────────── */

void IRAM_ATTR onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_running || type != WIFI_PKT_MGMT) return;

  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  const uint8_t* p = pkt->payload;

  /* Beacons only. Remote ID rides in the beacon the aircraft is already
   * transmitting; probe responses are not where the standard puts it. */
  if (len < 38 || p[0] != 0x80) return;

  /* 24-byte header, then 12 bytes of fixed beacon parameters (timestamp,
   * interval, capability) before the elements start. */
  size_t i = 36;
  while (i + 2 <= len) {
    const uint8_t id   = p[i];
    const uint8_t ilen = p[i + 1];
    if (i + 2 + ilen > len) return;        // element claims more than the frame
    if (id == DroneId::kWifiElementId && ilen >= 5 &&
        memcmp(p + i + 2, DroneId::kWifiOui, 3) == 0 &&
        p[i + 5] == DroneId::kWifiOuiType) {
      const uint8_t total = (uint8_t)(ilen + 2);
      if (total <= kQueueBytes) {
        const uint8_t next = (uint8_t)((s_qHead + 1) % kQueueSlots);
        if (next == s_qTail) {
          s_qDropped++;                     // queue full; the loop is behind
        } else {
          Slot& s = s_queue[s_qHead];
          memcpy(s.bytes, p + i, total);
          s.len = total;
          memcpy(s.mac, p + 10, 6);         // addr2, the transmitter
          s.rssi = pkt->rx_ctrl.rssi;
          s_qHead = next;
        }
      }
      return;                               // one Remote ID element per frame
    }
    i += 2 + ilen;
  }
}

void hopChannel() {
  s_chan++;
  if (s_chan > kChanLast) s_chan = kChanFirst;
  esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);
}

/* ── BLE ─────────────────────────────────────────────────────────────────── */

class AdvCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* dev) override {
    if (!s_running || dev == nullptr) return;
    const BLEUUID uuid((uint16_t)DroneId::kBleServiceUuid);
    if (!dev->haveServiceData()) return;
    const std::string sd = dev->getServiceData(uuid);
    if (sd.empty()) return;

    uint8_t mac[6];
    memcpy(mac, dev->getAddress().getNative(), 6);
    ingest((const uint8_t*)sd.data(), sd.size(), mac,
           (int8_t)dev->getRSSI(), true);
  }
};

AdvCallbacks s_advCb;

/* ── UI ──────────────────────────────────────────────────────────────────── */

const char* uaTypeText(uint8_t t) {
  switch (t) {
    case 1:  return "aeroplane";
    case 2:  return "multirotor";
    case 3:  return "gyroplane";
    case 4:  return "hybrid";
    case 5:  return "ornithopter";
    case 6:  return "glider";
    case 7:  return "kite";
    case 8:  return "free balloon";
    case 9:  return "captive balloon";
    case 10: return "airship";
    case 11: return "parachute";
    case 12: return "rocket";
    case 13: return "tethered";
    case 14: return "ground obstacle";
    default: return "unspecified";
  }
}

const char* statusText(uint8_t s) {
  switch (s) {
    case 1:  return "ground";
    case 2:  return "airborne";
    case 3:  return "emergency";
    case 4:  return "RID fault";
    default: return "undeclared";
  }
}

void drawHeader() {
  tft.setTextFont(1);
  tft.setTextSize(1);

  char buf[56];
  snprintf(buf, sizeof(buf), "ch %-2u  seen %-4lu  craft %u",
           (unsigned)s_chan, (unsigned long)s_frames, (unsigned)s_craftCount);
  uiShowLine(s_shownHdr[0], sizeof(s_shownHdr[0]), buf,
             8, PUEO_STATUS_SHORT + 4, 11, UI_TEXT, TFT_BLACK);

  /* The limit, on screen rather than in a README. An empty list here does
   * not mean the sky is empty. */
  if (uiShowLine(s_shownHdr[1], sizeof(s_shownHdr[1]),
                 "WiFi beacon + BLE4 only - not BT5 long range", 8,
                 PUEO_STATUS_SHORT + 16, 11, UI_DIM_TEXT, TFT_BLACK)) {
    tft.drawFastHLine(0, PUEO_STATUS_SHORT + 30, PUEO_SCREEN_W, UI_LINE);
  }
}

void drawList() {
  const int top = PUEO_STATUS_SHORT + 34;
  const int bottom = contentBottom();
  tft.setTextFont(1);
  tft.setTextSize(1);

  if (s_craftCount == 0) {
    uiShowLine(s_shownRow[0][0], sizeof(s_shownRow[0][0]), "listening...",
               8, top + 6, 11, UI_DIM_TEXT, TFT_BLACK);
    return;
  }

  const uint32_t now = millis();
  int y = top + 2;
  const int rowH = 40;
  uint8_t i = 0;

  for (; i < s_craftCount && y + rowH <= bottom; i++) {
    const Craft& c = s_craft[i];
    if (!c.used) continue;
    const DroneId::Report& r = c.rep;

    char line[64];

    /* An aircraft heard once and not since is not the same as one overhead
     * now, and the difference matters when you are looking up. */
    const uint32_t ageS = (now - c.lastMs) / 1000u;
    const uint16_t colour = (ageS <= 5) ? UI_OK : UI_DIM_TEXT;

    /* The identity line owns its band, so the radio/RSSI tag beside it is
     * repainted whenever that line was redrawn under it. */
    snprintf(line, sizeof(line), "%s", r.uasId[0] ? r.uasId : "(no ID yet)");
    const bool idRedrawn = uiShowLine(s_shownRow[i][0], sizeof(s_shownRow[i][0]),
                                      line, 8, y, 11, colour, TFT_BLACK);

    snprintf(line, sizeof(line), "%s %d", c.viaBle ? "BLE" : "WiFi", (int)c.rssi);
    if (idRedrawn || strncmp(s_shownRow[i][3], line,
                             sizeof(s_shownRow[i][3]) - 1) != 0) {
      tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
      tft.drawString(line, PUEO_SCREEN_W - 76, y);
      snprintf(s_shownRow[i][3], sizeof(s_shownRow[i][3]), "%s", line);
    }

    if (r.haveLocation) {
      snprintf(line, sizeof(line), "%.5f %.5f  %.0fm",
               r.latitude, r.longitude, (double)r.altitudeGeo);
    } else {
      snprintf(line, sizeof(line), "%s, no position sent", uaTypeText(r.uaType));
    }
    uiShowLine(s_shownRow[i][1], sizeof(s_shownRow[i][1]), line,
               8, y + 11, 11, UI_TEXT, TFT_BLACK);

    if (r.haveLocation) {
      if (r.speedHorizontal >= 0.0f) {
        snprintf(line, sizeof(line), "%s  %.0f m/s  %s",
                 statusText(r.status), (double)r.speedHorizontal,
                 uaTypeText(r.uaType));
      } else {
        snprintf(line, sizeof(line), "%s  %s", statusText(r.status),
                 uaTypeText(r.uaType));
      }
    } else {
      snprintf(line, sizeof(line), "%s", statusText(r.status));
    }
    uiShowLine(s_shownRow[i][2], sizeof(s_shownRow[i][2]), line,
               8, y + 21, 11, UI_DIM_TEXT, TFT_BLACK);

    if (r.haveOperator) {
      snprintf(line, sizeof(line), "operator %.5f %.5f",
               r.operatorLatitude, r.operatorLongitude);
    } else {
      snprintf(line, sizeof(line), "%us ago  %u frames",
               (unsigned)ageS, (unsigned)c.frames);
    }
    uiShowLine(s_shownRow[i][4], sizeof(s_shownRow[i][4]), line,
               8, y + 31, 11, UI_DIM_TEXT, TFT_BLACK);

    y += rowH;
  }

  /* Rows that existed a moment ago and do not now. Per-line caching never
   * repaints a row it no longer draws, so without this the tail of a longer
   * list stays on screen. */
  for (; i < kMaxCraft && y + rowH <= bottom; i++) {
    if (s_shownRow[i][0][0] != 0) {
      tft.fillRect(0, y, PUEO_SCREEN_W, rowH, TFT_BLACK);
      memset(s_shownRow[i], 0, sizeof(s_shownRow[i]));
    }
    y += rowH;
  }
}

}  // namespace

void setup() {
  showFeatureMark(bitmap_pueo_hunt, "Drone Detector");

  memset(s_craft, 0, sizeof(s_craft));
  s_craftCount = 0;
  s_chan = kChanFirst;
  s_frames = 0;
  s_qHead = s_qTail = 0;
  s_qDropped = 0;
  s_dirty = true;

  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(60);
  esp_wifi_set_promiscuous(false);
  {
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
  }
  esp_wifi_set_promiscuous_rx_cb(&onPacket);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);

  if (ensureBleStackReady()) {
    s_scan = BLEDevice::getScan();
    if (s_scan) {
      s_scan->setAdvertisedDeviceCallbacks(&s_advCb, true);
      s_scan->setActiveScan(false);   // passive: never ask, only listen
      /* Window under interval, on purpose.
       *
       * The ESP32 has one radio. A BLE scan whose window equals its interval
       * asks for it continuously, and the coexistence arbiter then gives the
       * WiFi side almost nothing -- which is fine for a BLE-only feature and
       * ruinous for this one, because it listens on both.
       *
       * Measured with the bench beacon, which counts what it sends: 495 ALPR
       * probe requests transmitted, and this screen found the first one after
       * about five minutes. The frames were going out; there was no receiver
       * awake to hear them. Hunt and Fast Pair keep a full window because
       * they have no WiFi side to starve.
       *
       * 100 ms interval with a 50 ms window is half the airtime each. BLE
       * sightings get rarer in exchange, which is the trade. */
      s_scan->setInterval(160);   // 160 * 0.625 ms = 100 ms
      s_scan->setWindow(80);      //  80 * 0.625 ms =  50 ms
    }
  }

  s_running = true;
  s_lastHop = millis();
  s_lastDraw = 0;

  tft.fillScreen(TFT_BLACK);
  setStatusBarHeight(PUEO_STATUS_SHORT);
  drawStatusBar(readBatteryVoltage(), true);
  drawHeader();
  drawList();
  redrawTouchButtonBar();
}

void loop() {
  const uint32_t now = millis();

  if ((uint32_t)(now - s_lastHop) >= kHopMs) {
    s_lastHop = now;
    hopChannel();
    s_dirty = true;
  }

  if (s_scan && !s_scan->isScanning()) {
    s_scan->start(kBleWindowMs / 1000, nullptr, false);
  }

  /* Drain what the callback copied out. */
  while (s_qTail != s_qHead) {
    Slot& s = s_queue[s_qTail];
    ingest(s.bytes, s.len, s.mac, s.rssi, false);
    s_qTail = (uint8_t)((s_qTail + 1) % kQueueSlots);
  }

  if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
    s_lastDraw = now;
    s_dirty = false;
    drawHeader();
    drawList();
  }

  delay(4);
}

void exit() {
  s_running = false;

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);

  if (s_scan) {
    s_scan->stop();
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
    s_scan->clearResults();
    s_scan = nullptr;
  }

  WiFi.mode(WIFI_STA);
}

}  // namespace DroneScan
