#include "TrackerHunt.h"

#include "SpotterSignatures.h"
#include "config.h"
#include "shared.h"
#include "utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace TrackerHunt {
namespace {

/* ── Tuning ──────────────────────────────────────────────────────────────── */

/* The needle's range. -100 dBm is about as weak as a scan reports before the
 * sighting simply stops arriving; -35 is what a tracker reads at arm's
 * length with nothing in the way. Everything outside clamps, so standing on
 * top of one pins the needle rather than wrapping it round. */
constexpr int kRssiFar  = -100;
constexpr int kRssiNear = -35;

/* Smoothing. Raw RSSI on consecutive advertisements from a stationary device
 * swings 6-8 dB, which on this gauge is a needle that never settles and
 * cannot be read while walking. A quarter weight on each new sighting is
 * slow enough to read and fast enough to answer a step. */
constexpr float kAlpha = 0.25f;

/* After this long with nothing heard the lock is stale. Find My advertises
 * every 2 s or so when separated, Tile every 1 s, so three seconds of
 * silence is a device that has gone, been shielded, or rotated its address
 * out from under us. */
constexpr uint32_t kLostMs = 3000;

/* A row drops off the picker after a minute without a sighting. */
constexpr uint32_t kForgetMs = 60000;

constexpr uint32_t kRedrawMs = 60;
constexpr int      kRowH     = 22;

/* ── State ───────────────────────────────────────────────────────────────── */

enum class Screen : uint8_t { Pick, Gauge };

Target   s_targets[kMaxTargets];
int      s_count    = 0;
int      s_sel      = 0;
int      s_scroll   = 0;
Screen   s_screen   = Screen::Pick;
bool     s_running  = false;
bool     s_dirty    = true;
bool     s_chrome   = false;      // gauge's static parts are drawn
uint32_t s_lastDraw = 0;

uint8_t  s_lockMac[6] = {0};
char     s_lockLabel[18] = {0};
float    s_smooth   = (float)kRssiFar;
int8_t   s_lastRssi = kRssiFar;
int8_t   s_peak     = kRssiFar;
uint32_t s_lockSeen = 0;
uint32_t s_lockHits = 0;
int      s_prevAngle = -1;

BLEScan* s_scan = nullptr;

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

/* ── Recognising a tracker ───────────────────────────────────────────────── */

/* Apple's Continuity type byte. 0x12 is "Find My" -- an offline-finding
 * broadcast from a device that has been separated from its owner, which is
 * both an AirTag and any other Find My accessory, and is the thing worth
 * hunting. 0x07 and friends are proximity pairing and are not. */
constexpr uint16_t kAppleCompany = 0x004C;
constexpr uint8_t  kFindMyType   = 0x12;

/* Fills `label` and returns true when this advertisement is a tracker.
 *
 * The service-UUID side reads Spotter's table rather than keeping a second
 * copy: SpotterSignatures.h is where a tracker UUID gets added, and a
 * private list here would be the same fact in two places, which is the bug
 * this codebase keeps shipping. Only Kind::Tracker rows are consulted. */
bool identify(BLEAdvertisedDevice* dev, char* label, size_t labelSz) {
  if (dev->haveManufacturerData()) {
    const std::string md = dev->getManufacturerData();
    if (md.size() >= 3) {
      const uint16_t company = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));
      if (company == kAppleCompany && (uint8_t)md[2] == kFindMyType) {
        snprintf(label, labelSz, "Find My");
        return true;
      }
    }
  }

  const uint8_t count = dev->getServiceUUIDCount();
  for (uint8_t i = 0; i < count; i++) {
    const NimBLEUUID u = dev->getServiceUUID(i);
    if (u.bitSize() != 16) {
      continue;
    }
    const uint16_t svc = (uint16_t)u.getNative()->u16.value;
    for (size_t j = 0; j < Spotter::kBleSigCount; j++) {
      const Spotter::BleSig& sig = Spotter::kBleSigs[j];
      if (sig.kind != Spotter::Kind::Tracker || sig.service != svc) {
        continue;
      }
      snprintf(label, labelSz, "%s", sig.label);
      return true;
    }
  }
  return false;
}

void record(const uint8_t* mac, int8_t rssi, const char* label) {
  const uint32_t now = millis();

  for (int i = 0; i < s_count; i++) {
    if (memcmp(s_targets[i].mac, mac, 6) != 0) {
      continue;
    }
    s_targets[i].rssi = rssi;
    if (rssi > s_targets[i].best) {
      s_targets[i].best = rssi;
    }
    s_targets[i].lastSeen = now;
    if (s_targets[i].hits < 0xFFFF) {
      s_targets[i].hits++;
    }
    return;
  }

  int slot = s_count;
  if (slot >= kMaxTargets) {
    /* Full. Replace the weakest, which is the one least likely to be what
     * somebody is standing here hunting for. */
    int worst = 0;
    for (int i = 1; i < kMaxTargets; i++) {
      if (s_targets[i].rssi < s_targets[worst].rssi) {
        worst = i;
      }
    }
    if (s_targets[worst].rssi >= rssi) {
      return;
    }
    slot = worst;
  } else {
    s_count++;
  }

  memset(&s_targets[slot], 0, sizeof(s_targets[slot]));
  memcpy(s_targets[slot].mac, mac, 6);
  s_targets[slot].rssi     = rssi;
  s_targets[slot].best     = rssi;
  s_targets[slot].lastSeen = now;
  s_targets[slot].hits     = 1;
  snprintf(s_targets[slot].label, sizeof(s_targets[slot].label), "%s", label);
}

class HuntCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* dev) override {
    if (!s_running || !dev) {
      return;
    }

    char label[18];
    if (!identify(dev, label, sizeof(label))) {
      return;
    }

    uint8_t mac[6] = {0};
    const std::string addr = dev->getAddress().toString();
    unsigned b[6] = {0};
    if (sscanf(addr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
      return;
    }
    for (int i = 0; i < 6; i++) {
      mac[i] = (uint8_t)b[i];
    }

    const int8_t rssi = (int8_t)dev->getRSSI();
    record(mac, rssi, label);

    if (s_screen == Screen::Gauge && memcmp(mac, s_lockMac, 6) == 0) {
      s_lastRssi = rssi;
      s_smooth   = s_smooth * (1.0f - kAlpha) + (float)rssi * kAlpha;
      if (rssi > s_peak) {
        s_peak = rssi;
      }
      s_lockSeen = millis();
      s_lockHits++;
    }
  }
};

HuntCallbacks s_cb;

/* ── Picker ──────────────────────────────────────────────────────────────── */

void forgetStale() {
  const uint32_t now = millis();
  int w = 0;
  for (int i = 0; i < s_count; i++) {
    if ((uint32_t)(now - s_targets[i].lastSeen) < kForgetMs) {
      if (w != i) {
        s_targets[w] = s_targets[i];
      }
      w++;
    }
  }
  if (w != s_count) {
    s_count = w;
    if (s_sel >= s_count) {
      s_sel = s_count > 0 ? s_count - 1 : 0;
    }
  }
}

void sortByRssi() {
  for (int i = 1; i < s_count; i++) {
    Target t = s_targets[i];
    int j = i - 1;
    while (j >= 0 && s_targets[j].rssi < t.rssi) {
      s_targets[j + 1] = s_targets[j];
      j--;
    }
    s_targets[j + 1] = t;
  }
}

void drawPicker() {
  const int top    = 22;
  const int bottom = contentBottom();
  const int rows   = (bottom - top - 18) / kRowH;

  tft.fillRect(0, top, PUEO_SCREEN_W, bottom - top, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "trackers in range: %d", s_count);
  tft.drawString(hdr, 8, top + 2);

  if (s_count == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("listening...", 8, top + 24);
    tft.drawString("Find My, Tile, SmartTag, Eddystone", 8, top + 38);
    return;
  }

  if (s_sel < s_scroll) {
    s_scroll = s_sel;
  }
  if (s_sel >= s_scroll + rows) {
    s_scroll = s_sel - rows + 1;
  }

  const uint32_t now = millis();
  int y = top + 18;
  for (int i = 0; i < rows && (s_scroll + i) < s_count; i++) {
    const Target& t = s_targets[s_scroll + i];
    const bool sel = (s_scroll + i) == s_sel;

    if (sel) {
      tft.fillRect(0, y, PUEO_SCREEN_W, kRowH, 0x2124);
    }
    tft.setTextColor(sel ? ORANGE : TFT_WHITE, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(t.label, 8, y + 2);

    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             t.mac[0], t.mac[1], t.mac[2], t.mac[3], t.mac[4], t.mac[5]);
    tft.setTextColor(sel ? ORANGE : TFT_DARKGREY, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(mac, 8, y + 12);

    char right[24];
    snprintf(right, sizeof(right), "%4d dBm  %2lus",
             (int)t.rssi, (unsigned long)((now - t.lastSeen) / 1000));
    tft.setTextColor(sel ? ORANGE : TFT_WHITE, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(right, PUEO_SCREEN_W - 96, y + 6);

    y += kRowH;
  }
}

/* ── Gauge ───────────────────────────────────────────────────────────────── */

struct Dial {
  int cx, cy, r;
};

Dial dial() {
  const int top    = 30;
  const int bottom = contentBottom();
  Dial d;
  d.cx = PUEO_SCREEN_W / 2;
  /* The arc is the top half of a circle, so it needs r of height, and the
   * readout below needs about 46. Whichever of width and height runs out
   * first sets the radius. */
  const int byWidth  = PUEO_SCREEN_W / 2 - 10;
  const int byHeight = (bottom - top - 46);
  d.r  = byWidth < byHeight ? byWidth : byHeight;
  d.cy = top + d.r;
  return d;
}

/* RSSI to needle angle in degrees: 180 is hard left and far, 0 is hard right
 * and near. */
int angleFor(float rssi) {
  if (rssi < (float)kRssiFar)  rssi = (float)kRssiFar;
  if (rssi > (float)kRssiNear) rssi = (float)kRssiNear;
  const float t = (rssi - (float)kRssiFar) /
                  (float)(kRssiNear - kRssiFar);   // 0 far .. 1 near
  return (int)(180.0f - t * 180.0f + 0.5f);
}

void polar(const Dial& d, int deg, int radius, int& x, int& y) {
  const float a = (float)deg * (float)M_PI / 180.0f;
  x = d.cx + (int)(cosf(a) * (float)radius + 0.5f);
  y = d.cy - (int)(sinf(a) * (float)radius + 0.5f);
}

void drawArc(const Dial& d) {
  for (int deg = 0; deg <= 180; deg += 2) {
    int x, y;
    polar(d, deg, d.r, x, y);
    /* Right of the dial is near, and near is what you are looking for, so
     * the arc warms up towards it. */
    uint16_t col = TFT_DARKGREY;
    if (deg <= 40)       col = TFT_RED;
    else if (deg <= 80)  col = ORANGE;
    tft.drawPixel(x, y, col);
  }
  for (int deg = 0; deg <= 180; deg += 30) {
    int x0, y0, x1, y1;
    polar(d, deg, d.r, x0, y0);
    polar(d, deg, d.r - 8, x1, y1);
    tft.drawLine(x0, y0, x1, y1, TFT_DARKGREY);
  }
}

void drawNeedle(const Dial& d, int deg, uint16_t col) {
  int tx, ty, bx0, by0, bx1, by1;
  polar(d, deg, d.r - 10, tx, ty);
  polar(d, (deg + 90) % 360, 5, bx0, by0);
  polar(d, (deg + 270) % 360, 5, bx1, by1);
  tft.fillTriangle(tx, ty, bx0, by0, bx1, by1, col);
}

void drawGaugeChrome(const Dial& d) {
  tft.fillRect(0, 22, PUEO_SCREEN_W, contentBottom() - 22, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.drawString(s_lockLabel, 8, 24);

  char mac[20];
  snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_lockMac[0], s_lockMac[1], s_lockMac[2],
           s_lockMac[3], s_lockMac[4], s_lockMac[5]);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(mac, PUEO_SCREEN_W - 104, 24);

  drawArc(d);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("FAR", d.cx - d.r, d.cy + 4);
  tft.drawString("NEAR", d.cx + d.r - 20, d.cy + 4);
  tft.fillCircle(d.cx, d.cy, 3, TFT_DARKGREY);
}

void drawGauge() {
  const Dial d = dial();
  const uint32_t now = millis();
  const bool lost = (uint32_t)(now - s_lockSeen) > kLostMs;

  if (!s_chrome) {
    drawGaugeChrome(d);
    s_chrome = true;
    s_prevAngle = -1;
  }

  const int deg = angleFor(lost ? (float)kRssiFar : s_smooth);

  if (deg != s_prevAngle) {
    if (s_prevAngle >= 0) {
      drawNeedle(d, s_prevAngle, TFT_BLACK);
    }
    drawArc(d);                       // the erase above clips it
    tft.fillCircle(d.cx, d.cy, 3, TFT_DARKGREY);

    /* Peak marker: the best reading of this hunt, so a needle that has
     * fallen back still says where it got to. */
    if (s_peak > kRssiFar) {
      int px0, py0, px1, py1;
      const int pdeg = angleFor((float)s_peak);
      polar(d, pdeg, d.r, px0, py0);
      polar(d, pdeg, d.r - 12, px1, py1);
      tft.drawLine(px0, py0, px1, py1, TFT_GREEN);
    }

    drawNeedle(d, deg, lost ? TFT_DARKGREY : TFT_RED);
    s_prevAngle = deg;
  }

  const int ty = d.cy + 16;
  tft.fillRect(0, ty, PUEO_SCREEN_W, 30, TFT_BLACK);
  tft.setTextFont(1);

  if (lost) {
    tft.setTextSize(2);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("NO SIGNAL", d.cx, ty, 1);
    tft.setTextSize(1);
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawCentreString("moved off, shielded, or changed address",
                         d.cx, ty + 18, 1);
    return;
  }

  char big[16];
  snprintf(big, sizeof(big), "%d dBm", (int)(s_smooth - 0.5f));
  tft.setTextSize(2);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawCentreString(big, d.cx, ty, 1);

  char sub[48];
  snprintf(sub, sizeof(sub), "now %d   best %d   %lu seen",
           (int)s_lastRssi, (int)s_peak, (unsigned long)s_lockHits);
  tft.setTextSize(1);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawCentreString(sub, d.cx, ty + 18, 1);
}

void enterGauge() {
  const Target& t = s_targets[s_sel];
  memcpy(s_lockMac, t.mac, 6);
  snprintf(s_lockLabel, sizeof(s_lockLabel), "%s", t.label);
  s_smooth    = (float)t.rssi;
  s_lastRssi  = t.rssi;
  s_peak      = t.rssi;
  s_lockSeen  = millis();
  s_lockHits  = 0;
  s_chrome    = false;
  s_prevAngle = -1;
  s_screen    = Screen::Gauge;
  setTouchNavLabels("List", "", "", "", "Reset");
  s_dirty = true;
}

void enterPicker() {
  s_screen = Screen::Pick;
  setTouchNavLabels("Back", "Down", "", "Up", "Hunt");
  s_dirty = true;
}

}  // namespace

/* ── Entry points ────────────────────────────────────────────────────────── */

void setup() {
  s_count   = 0;
  s_sel     = 0;
  s_scroll  = 0;
  s_chrome  = false;
  s_dirty   = true;
  s_lastDraw = 0;
  memset(s_targets, 0, sizeof(s_targets));

  setTouchButtonInputEnabled(true);
  enterPicker();

  if (ensureBleStackReady()) {
    s_scan = BLEDevice::getScan();
    if (s_scan) {
      s_scan->setAdvertisedDeviceCallbacks(&s_cb, true);
      /* Passive. A hunt is for something already broadcasting; asking it to
       * say more would be transmitting, which this tree does not do from a
       * detection feature. */
      s_scan->setActiveScan(false);
      /* Window equal to interval: listen continuously rather than duty
       * cycling. The needle's job is to answer a step within a second. */
      s_scan->setInterval(80);
      s_scan->setWindow(80);
    }
  }

  s_running = true;
  drawStatusBar(readBatteryVoltage(), true);
}

void loop() {
  const uint32_t now = millis();

  if (s_scan && !s_scan->isScanning()) {
    s_scan->start(1, nullptr, false);
  }

  if (s_screen == Screen::Pick) {
    if (isButtonPressed(BTN_UP)) {
      if (s_sel > 0) s_sel--;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_DOWN)) {
      if (s_sel + 1 < s_count) s_sel++;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_RIGHT)) {
      if (s_count > 0) {
        enterGauge();
      }
      delay(200);
      while (isButtonPressed(BTN_RIGHT)) {
      }
    }
    forgetStale();
    sortByRssi();
  } else {
    if (isButtonPressed(BTN_LEFT)) {
      enterPicker();
      delay(200);
      while (isButtonPressed(BTN_LEFT)) {
      }
    } else if (isButtonPressed(BTN_RIGHT)) {
      s_peak = s_lastRssi;
      s_prevAngle = -1;
      s_dirty = true;
      delay(200);
      while (isButtonPressed(BTN_RIGHT)) {
      }
    }
    s_dirty = true;         // the needle is live; redraw on the timer
  }

  if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
    s_lastDraw = now;
    s_dirty = false;
    if (s_screen == Screen::Pick) {
      drawPicker();
    } else {
      drawGauge();
    }
  }

  delay(4);
}

void exit() {
  s_running = false;
  if (s_scan) {
    s_scan->stop();
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
    s_scan->clearResults();
    s_scan = nullptr;
  }
  requestStatusBarRedraw();
}

}  // namespace TrackerHunt
