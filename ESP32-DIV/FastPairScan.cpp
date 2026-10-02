#include "FastPairScan.h"

#include "BleCompat.h"
#include "FastPair.h"
#include "FastPairProbe.h"
#include "Stealth.h"
#include "config.h"
#include "shared.h"
#include "utils.h"

#include <string.h>

namespace FastPairScan {

namespace {

constexpr int      kMaxDevices = 24;
/* Three lines to a row, same shape as Surveillance. The 2.8" numbers are
 * the originals and are kept exactly. */
constexpr int      kRowH       = 54;
constexpr int      kLine2      = 18;   /* second line, from the row top */
constexpr int      kLine3      = 36;
constexpr uint32_t kRedrawMs   = 400;
constexpr uint16_t kProbeMs    = 4000;   // how long to wait for a notification

/* One advertiser, keyed on its address alone. See the note in the header
 * about why nothing is merged across addresses. */
struct Device {
  uint8_t  mac[6];
  bool     addrPublic;
  FastPair::Frame frame;
  uint32_t modelId;
  int8_t   rssiBest;
  int8_t   rssiLast;
  uint16_t seen;
  uint32_t firstMs;
  uint32_t lastMs;
  uint8_t  battLevel[3];
  uint8_t  battCount;
  bool     battCharging[3];
  uint8_t  filterLen;
  bool     haveSalt;
};

Device  s_dev[kMaxDevices];
int     s_devCount = 0;
int     s_sel      = 0;
int     s_scroll   = 0;
uint32_t s_adverts = 0;

bool     s_dirty   = true;
uint32_t s_lastDraw = 0;

BLEScan* s_scan = nullptr;

portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* Screens. The probe gets its own so that what it is about to do is on
 * screen before it does it, rather than in a doc nobody has open. */
enum class View : uint8_t { List, Confirm, Running, Result };
View s_view = View::List;

FastPairProbe::Report s_report;
uint8_t s_target[6];
bool    s_targetPublic = false;

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

/* ── collection ──────────────────────────────────────────────────────────── */

void upsert(const uint8_t mac[6], bool isPublic, int8_t rssi,
            const FastPair::Adv& adv) {
  const uint32_t now = millis();
  int idx = -1;
  for (int i = 0; i < s_devCount; i++) {
    if (memcmp(s_dev[i].mac, mac, 6) == 0) {
      idx = i;
      break;
    }
  }
  if (idx < 0) {
    if (s_devCount >= kMaxDevices) {
      /* Full. Drop the row heard from least recently rather than the
       * newest arrival, so a busy room does not hide the device that just
       * walked in. */
      int oldest = 0;
      for (int i = 1; i < s_devCount; i++) {
        if ((int32_t)(s_dev[i].lastMs - s_dev[oldest].lastMs) < 0) {
          oldest = i;
        }
      }
      idx = oldest;
      memset(&s_dev[idx], 0, sizeof(s_dev[idx]));
    } else {
      idx = s_devCount++;
      memset(&s_dev[idx], 0, sizeof(s_dev[idx]));
    }
    memcpy(s_dev[idx].mac, mac, 6);
    s_dev[idx].addrPublic = isPublic;
    s_dev[idx].firstMs = now;
    s_dev[idx].rssiBest = rssi;
  }

  Device& d = s_dev[idx];
  d.lastMs = now;
  d.rssiLast = rssi;
  if (rssi > d.rssiBest) {
    d.rssiBest = rssi;
  }
  if (d.seen < 0xFFFF) {
    d.seen++;
  }
  d.frame = adv.frame;
  if (adv.frame == FastPair::Frame::ModelId) {
    d.modelId = adv.modelId;
  }
  d.filterLen = adv.filterLen;
  d.haveSalt = adv.haveSalt;
  d.battCount = adv.battery.present ? adv.battery.count : 0;
  for (int i = 0; i < 3; i++) {
    d.battLevel[i] = adv.battery.level[i];
    d.battCharging[i] = adv.battery.charging[i];
  }
}

class AdvCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* device) override {
    if (device == nullptr || s_view != View::List) {
      return;
    }
    const std::string sd =
        device->getServiceData(BLEUUID((uint16_t)FastPair::kUuidFastPair));
    if (sd.empty()) {
      return;
    }
    FastPair::Adv adv;
    if (!FastPair::parse(reinterpret_cast<const uint8_t*>(sd.data()),
                         sd.size(), &adv)) {
      return;
    }

    const uint8_t* raw = device->getAddress().getNative();
    /* NimBLE hands back the address little-endian; the rest of this file,
     * and the probe's request field, want it the way it is written down. */
    uint8_t mac[6];
    for (int i = 0; i < 6; i++) {
      mac[i] = raw[5 - i];
    }
    const bool isPublic = (device->getAddress().getType() == BLE_ADDR_PUBLIC);

    portENTER_CRITICAL(&s_mux);
    s_adverts++;
    upsert(mac, isPublic, (int8_t)device->getRSSI(), adv);
    s_dirty = true;
    portEXIT_CRITICAL(&s_mux);
  }
};

AdvCallbacks s_advCb;

/* ── drawing ─────────────────────────────────────────────────────────────── */

uint16_t frameColour(FastPair::Frame f) {
  switch (f) {
    case FastPair::Frame::ModelId:    return TFT_GREEN;   // in pairing mode
    case FastPair::Frame::AccountKey: return TFT_CYAN;    // belongs to someone
    case FastPair::Frame::Empty:      return TFT_LIGHTGREY;
    default:                          return TFT_DARKGREY;
  }
}

/* See uiShowLine in utils.h. Three lines per row, plus the row's selected
 * state, because the highlight changes every line's background. */
constexpr int kMaxVisRows = 16;
char s_shownRow[kMaxVisRows][3][44];
char s_shownSel[kMaxVisRows];
char s_shownHdr[44];

void forgetDrawn() {
  memset(s_shownRow, 0, sizeof(s_shownRow));
  memset(s_shownSel, 0, sizeof(s_shownSel));
  memset(s_shownHdr, 0, sizeof(s_shownHdr));
}

void drawHeader() {
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);
  char buf[40];
  snprintf(buf, sizeof(buf), "devices %d   adverts %lu",
           s_devCount, (unsigned long)s_adverts);
  uiShowLine(s_shownHdr, sizeof(s_shownHdr), buf, 8, 24, 18,
             TFT_WHITE, TFT_BLACK);
}

void drawList() {
  const int top = 42;
  const int bottom = contentBottom();
  const int rows = (bottom - top) / kRowH;

  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  if (s_devCount == 0) {
    uiShowLine(s_shownRow[0][0], sizeof(s_shownRow[0][0]), "listening...",
               8, top + 6, PUEO_BODY_H, TFT_DARKGREY, TFT_BLACK);
    uiShowLine(s_shownRow[0][1], sizeof(s_shownRow[0][1]),
               "no Fast Pair advertisements yet", 8, top + 6 + kLine2,
               PUEO_BODY_H, TFT_DARKGREY, TFT_BLACK);
    return;
  }

  if (s_sel >= s_devCount) {
    s_sel = s_devCount - 1;
  }
  if (s_sel < 0) {
    s_sel = 0;
  }
  if (s_sel < s_scroll) {
    s_scroll = s_sel;
  }
  if (s_sel >= s_scroll + rows) {
    s_scroll = s_sel - rows + 1;
  }
  if (s_scroll < 0) {
    s_scroll = 0;
  }

  /* A scroll makes every cached line describe a different device. */
  static int s_lastScroll = -1;
  if (s_scroll != s_lastScroll) {
    s_lastScroll = s_scroll;
    forgetDrawn();
  }

  int i = 0;
  for (; i < rows && i < kMaxVisRows && (s_scroll + i) < s_devCount; i++) {
    const Device& d = s_dev[s_scroll + i];
    const int y = top + i * kRowH;
    const bool selected = (s_scroll + i) == s_sel;
    const uint16_t bg = selected ? 0x18E3 : TFT_BLACK;

    /* The highlight is the background of all three lines, so a row whose
     * selected state changed has to repaint whatever its text says. */
    const char wantSel = selected ? 1 : 0;
    if (s_shownSel[i] != wantSel) {
      s_shownSel[i] = wantSel;
      tft.fillRect(0, y - 2, PUEO_SCREEN_W, kRowH - 2, bg);
      s_shownRow[i][0][0] = s_shownRow[i][1][0] = s_shownRow[i][2][0] = '\0';
    }

    /* Line 1: what state it is in, and the model if it told us. */
    char line[40];
    if (d.frame == FastPair::Frame::ModelId) {
      const char* name = FastPair::modelName(d.modelId);
      if (name != nullptr) {
        snprintf(line, sizeof(line), "PAIRING  %.28s", name);
      } else {
        snprintf(line, sizeof(line), "PAIRING  model %06lX",
                 (unsigned long)d.modelId);
      }
    } else if (d.frame == FastPair::Frame::AccountKey) {
      snprintf(line, sizeof(line), "paired   filter %u bytes",
               (unsigned)d.filterLen);
    } else {
      snprintf(line, sizeof(line), "paired   no account keys");
    }
    uiShowLine(s_shownRow[i][0], sizeof(s_shownRow[i][0]), line, 8, y,
               PUEO_BODY_H, frameColour(d.frame), bg);

    /* Line 2: the address, which is all the identity there is. */
    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X %s %ddBm",
             d.mac[0], d.mac[1], d.mac[2], d.mac[3], d.mac[4], d.mac[5],
             d.addrPublic ? "pub" : "rnd", (int)d.rssiLast);
    uiShowLine(s_shownRow[i][1], sizeof(s_shownRow[i][1]), line,
               8, y + kLine2, PUEO_BODY_H, TFT_LIGHTGREY, bg);

    /* Line 3: battery, if offered, then how long it has been around. */
    {
      char batt[22] = "";
      if (d.battCount > 0) {
        int n = snprintf(batt, sizeof(batt), "batt ");
        for (int b = 0; b < d.battCount && n < (int)sizeof(batt) - 5; b++) {
          if (d.battLevel[b] == FastPair::kLevelUnknown) {
            n += snprintf(batt + n, sizeof(batt) - n, "--/");
          } else {
            n += snprintf(batt + n, sizeof(batt) - n, "%u%s/",
                          (unsigned)d.battLevel[b],
                          d.battCharging[b] ? "+" : "");
          }
        }
        if (n > 0 && batt[n - 1] == '/') {
          batt[n - 1] = ' ';
        }
      }
      const uint32_t secs = (d.lastMs - d.firstMs) / 1000u;
      char age[10];
      if (secs < 60u) {
        snprintf(age, sizeof(age), "%lus", (unsigned long)secs);
      } else if (secs < 3600u) {
        snprintf(age, sizeof(age), "%lum", (unsigned long)(secs / 60u));
      } else {
        snprintf(age, sizeof(age), "%luh", (unsigned long)(secs / 3600u));
      }
      snprintf(line, sizeof(line), "%sx%u %s", batt, (unsigned)d.seen, age);
      uiShowLine(s_shownRow[i][2], sizeof(s_shownRow[i][2]), line,
                 8, y + kLine3, PUEO_BODY_H, TFT_DARKGREY, bg);
    }
  }

  /* Rows the list no longer has. */
  for (; i < rows && i < kMaxVisRows; i++) {
    if (s_shownRow[i][0][0] == '\0' && s_shownSel[i] == 0) continue;
    tft.fillRect(0, top + i * kRowH - 2, PUEO_SCREEN_W, kRowH, TFT_BLACK);
    memset(s_shownRow[i], 0, sizeof(s_shownRow[i]));
    s_shownSel[i] = 0;
  }
}

void drawConfirm() {
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  int y = 30;
  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.drawString("PROBE -- THIS TRANSMITS", 8, y);
  y += PUEO_BODY_LINE;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  char line[40];
  snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_target[0], s_target[1], s_target[2],
           s_target[3], s_target[4], s_target[5]);
  tft.drawString(line, 8, y);
  y += PUEO_BODY_LINE;

  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  const char* what[] = {
    "Connects to this one device and",
    "writes 80 bytes to its Fast Pair",
    "key-based pairing characteristic.",
    "",
    "It does not pair, write an account",
    "key, or transmit to anything else.",
    "",
    "A correct device stays silent. One",
    "that answers did not check the",
    "address in the request.",
    "",
    "Test your own devices, or ones you",
    "have permission to test.",
  };
  for (size_t i = 0; i < sizeof(what) / sizeof(what[0]); i++) {
    tft.drawString(what[i], 8, y);
    y += PUEO_BODY_LINE;
  }

  y += 6;
  /* Said before the confirm rather than after it. The probe refuses under
   * stealth either way, but a device that takes the confirmation and then
   * says no looks broken. */
  if (Stealth::on()) {
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.drawString("Stealth Mode is on: this will not run.", 8, y);
    y += PUEO_BODY_LINE;
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("Settings > Stealth Mode.  Back to cancel.", 8, y);
  } else {
    tft.setTextColor(TFT_GREEN, TFT_BLACK);
    tft.drawString("Right again to run.  Back to cancel.", 8, y);
  }
}

void drawRunning() {
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);
  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.drawString("probing...", 8, 40);
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  char line[40];
  snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_target[0], s_target[1], s_target[2],
           s_target[3], s_target[4], s_target[5]);
  tft.drawString(line, 8, 40 + PUEO_BODY_LINE);
}

void drawResult() {
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  int y = 30;
  char line[40];
  snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X",
           s_target[0], s_target[1], s_target[2],
           s_target[3], s_target[4], s_target[5]);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(line, 8, y);
  y += PUEO_BODY_GAP;

  uint16_t c = TFT_LIGHTGREY;
  if (s_report.outcome == FastPairProbe::Outcome::Responded) {
    c = TFT_RED;
  } else if (s_report.outcome == FastPairProbe::Outcome::NoResponse) {
    c = TFT_GREEN;
  } else if (s_report.outcome == FastPairProbe::Outcome::Refused) {
    c = UI_WARN;
  }
  tft.setTextColor(c, TFT_BLACK);
  tft.drawString(FastPairProbe::outcomeText(s_report.outcome), 8, y);
  y += PUEO_BODY_LINE;
  tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
  tft.drawString(s_report.detail, 8, y);
  y += PUEO_BODY_GAP;

  /* What the result does and does not establish. On screen rather than
   * only in a doc, because this is where somebody decides what to believe. */
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  if (s_report.outcome == FastPairProbe::Outcome::Responded) {
    const char* txt[] = {
      "It answered a handshake it could",
      "not have validated. That is the",
      "finding. Confirm it twice before",
      "reporting: a stray notification",
      "from another characteristic would",
      "look the same.",
    };
    for (size_t i = 0; i < sizeof(txt) / sizeof(txt[0]); i++) {
      tft.drawString(txt[i], 8, y);
      y += PUEO_BODY_LINE;
    }
  } else if (s_report.outcome == FastPairProbe::Outcome::NoResponse) {
    const char* txt[] = {
      "Silence is what a correct device",
      "does. It is NOT proof of safety:",
      "a busy, already-connected or",
      "out-of-range device is also",
      "silent. Re-run it close and idle",
      "before concluding anything.",
    };
    for (size_t i = 0; i < sizeof(txt) / sizeof(txt[0]); i++) {
      tft.drawString(txt[i], 8, y);
      y += PUEO_BODY_LINE;
    }
  }
  y += 6;

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  snprintf(line, sizeof(line), "%u ms", (unsigned)s_report.elapsedMs);
  tft.drawString(line, 8, y);
  y += PUEO_BODY_LINE;

  /* The bytes, so the screen is not the only record of what happened. */
  if (s_report.notifiedLen > 0) {
    tft.setTextColor(TFT_LIGHTGREY, TFT_BLACK);
    tft.drawString("notified:", 8, y);
    y += PUEO_BODY_LINE;
    char hex[40];
    int n = 0;
    /* 11 bytes at 3 chars each is 33, which fits the 38 a row holds from
     * x=8. The rest goes on a second line. */
    for (int i = 0; i < s_report.notifiedLen && i < 16; i++) {
      n += snprintf(hex + n, sizeof(hex) - n, "%02X ", s_report.notified[i]);
      if ((i % 11) == 10 || i == s_report.notifiedLen - 1 || i == 15) {
        tft.drawString(hex, 8, y);
        y += PUEO_BODY_LINE;
        n = 0;
        hex[0] = '\0';
      }
    }
  }

  y += 6;
  tft.setTextColor(TFT_GREEN, TFT_BLACK);
  tft.drawString("Back to the list.", 8, y);
}

void redraw(bool full) {
  if (full) {
    tft.fillScreen(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);
  }
  drawHeader();
  drawList();

  if (full) {
    /* See the note in Spotter's redraw(): the same bug was here, reachable
     * by backing out of the confirm or result view rather than by waiting
     * for a dwell alert. */
    redrawTouchButtonBar();
  }
}

void scanStart() {
  if (s_scan == nullptr) {
    return;
  }
  s_scan->setAdvertisedDeviceCallbacks(&s_advCb, true);
  /* Passive. A scan request would only announce this device to the room,
   * and Fast Pair puts everything worth reading in the advertisement. */
  s_scan->setActiveScan(false);
  s_scan->setInterval(160);
  s_scan->setWindow(160);
  s_scan->setDuplicateFilter(false);
}

void scanStop() {
  if (s_scan != nullptr) {
    s_scan->stop();
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
  }
}

}  // namespace

/* ── Feature entry points ────────────────────────────────────────────────── */

void fastPairSetup() {
  memset(s_dev, 0, sizeof(s_dev));
  s_devCount = 0;
  s_sel = 0;
  s_scroll = 0;
  s_adverts = 0;
  s_dirty = true;
  s_lastDraw = 0;
  s_view = View::List;
  memset(&s_report, 0, sizeof(s_report));
  memset(s_target, 0, sizeof(s_target));

  setTouchButtonInputEnabled(true);
  setTouchNavLabels(nullptr, "Down", "Exit", "Up", "Probe");

  if (ensureBleStackReady()) {
    s_scan = BLEDevice::getScan();
    scanStart();
  }
  redraw(true);

  /* Last, and after redraw(true). Storing the labels does not paint them,
   * and redraw(full) starts with fillScreen -- painting the bar before that
   * puts it on screen and then wipes it, which is how this read as "the
   * feature has no buttons" while the centre slot was quietly exiting. */
  redrawTouchButtonBar();
}

void fastPairLoop() {
  const uint32_t now = millis();

  if (s_view == View::List) {
    if (s_scan != nullptr && !s_scan->isScanning()) {
      s_scan->start(0, nullptr, false);
    }

    if (isButtonPressed(BTN_UP)) {
      s_sel--;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_DOWN)) {
      s_sel++;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_RIGHT)) {
      if (s_devCount > 0 && s_sel >= 0 && s_sel < s_devCount) {
        memcpy(s_target, s_dev[s_sel].mac, 6);
        s_targetPublic = s_dev[s_sel].addrPublic;
        s_view = View::Confirm;
        drawConfirm();
      }
      delay(200);
      waitForButtonRelease(BTN_RIGHT);
    }

    if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
      s_lastDraw = now;
      s_dirty = false;
      drawHeader();
      drawList();
    }
  } else if (s_view == View::Confirm) {
    if (isButtonPressed(BTN_RIGHT)) {
      delay(200);
      waitForButtonRelease(BTN_RIGHT);
      s_view = View::Running;
      drawRunning();
      /* The scan has to stop: NimBLE will not open a connection while it
       * is running, and leaving it up would also mean collecting rows
       * nobody is looking at. */
      scanStop();
      FastPairProbe::run(s_target, s_targetPublic, kProbeMs, &s_report);
      s_view = View::Result;
      drawResult();
    } else if (isButtonPressed(BTN_UP) || isButtonPressed(BTN_DOWN)) {
      s_view = View::List;
      redraw(true);
      delay(200);
    }
  } else if (s_view == View::Result) {
    if (isButtonPressed(BTN_UP) || isButtonPressed(BTN_DOWN) ||
        isButtonPressed(BTN_RIGHT)) {
      delay(200);
      waitForButtonRelease(BTN_RIGHT);
      s_view = View::List;
      scanStart();
      redraw(true);
    }
  }

  delay(4);
}

void exit() {
  scanStop();
  if (s_scan != nullptr) {
    s_scan->clearResults();
    s_scan = nullptr;
  }
  s_view = View::List;
  requestStatusBarRedraw();
}

int      deviceCount() { return s_devCount; }
uint32_t advertsSeen() { return s_adverts; }

}  // namespace FastPairScan
