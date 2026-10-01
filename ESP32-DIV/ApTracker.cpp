#include "ApTracker.h"

#include "SignalGauge.h"
#include "Stealth.h"
#include "config.h"
#include "shared.h"
#include "utils.h"

#include <WiFi.h>
#include <esp_wifi.h>
#include <string.h>
#include <stdio.h>

namespace ApTracker {
namespace {

/* Beacons arrive about every 102.4 ms, so three seconds of silence is thirty
 * missed beacons. That is well past a couple lost to interference and well
 * short of making someone wait to find out the AP has gone. */
constexpr uint32_t kLostMs = 3000;

constexpr uint32_t kRedrawMs = 60;

/* Two lines to a picker row, the same metrics TrackerHunt's picker uses so
 * the two screens read the same way. The 2.8" numbers are its originals;
 * that panel has never been booted. */
constexpr int kRowH     = 40;
constexpr int kRowLine2 = 20;
constexpr int kRowRight = 6;
/* The right-hand column of a picker row. The MAC on the line below is
 * seventeen characters from x=8 and ends at 212 px, so this is as far left
 * as the column can start on a 320 px panel. */
constexpr int kRightX = PUEO_SCREEN_W - 104;


struct Ap {
  uint8_t  bssid[6];
  char     ssid[24];
  int8_t   rssi;
  uint8_t  channel;
  bool     hidden;
};

enum class Screen : uint8_t { Pick, Gauge };

Screen   s_screen = Screen::Pick;
Ap       s_aps[kMaxAps];
int      s_count  = 0;
int      s_sel    = 0;
int      s_scroll = 0;
bool     s_dirty  = true;
uint32_t s_lastDraw = 0;
bool     s_running  = false;

/* The lock. Written by loop(), read by the promiscuous callback, which runs
 * on the Wi-Fi task. */
volatile bool     s_locked   = false;
uint8_t           s_lockBssid[6] = {0};
char              s_lockSsid[24] = {0};
uint8_t           s_lockChannel  = 0;
volatile int8_t   s_lockRssi = -100;
volatile uint32_t s_lockSeen = 0;
volatile uint32_t s_lockHits = 0;

/* Promiscuous mode was ours to turn on, so it is ours to turn off. */
bool s_promiscOn = false;

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

/* ── Listening ────────────────────────────────────────────────────────────
 *
 * A beacon is a management frame with subtype 8, so the first byte of the
 * 802.11 header is 0x80. Address 3 is the BSSID, at offset 16.
 *
 * Matching on address 3 rather than address 2 matters: for a beacon they are
 * the same, but a mesh or repeater backhaul can put a different transmitter
 * address on a frame carrying the BSSID we locked, and it is the BSSID the
 * user picked.
 */
constexpr int kBssidOffset = 16;
constexpr int kMinMgmtLen  = 24;

void IRAM_ATTR onFrame(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (type != WIFI_PKT_MGMT || !s_locked) {
    return;
  }
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  if (pkt->rx_ctrl.sig_len < kMinMgmtLen + 6) {
    return;
  }
  const uint8_t* p = pkt->payload;
  if (p[0] != 0x80) {                      // beacon only
    return;
  }
  if (memcmp(p + kBssidOffset, s_lockBssid, 6) != 0) {
    return;
  }

  s_lockRssi = (int8_t)pkt->rx_ctrl.rssi;
  s_lockSeen = millis();
  s_lockHits++;
}

void stopListening() {
  if (!s_promiscOn) {
    return;
  }
  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);
  s_promiscOn = false;
}

/* Park on one channel and listen. No hopping: hopping is what every other
 * Wi-Fi feature here does and it is exactly wrong for this one, because a
 * sample rate of "whenever we happen to be on the right channel" is the
 * problem this feature exists to avoid. */
void startListening(uint8_t channel) {
  stopListening();

  /* State what we want rather than inherit it. The filter is global and
   * sticky, and a feature that leaves MASK_MGMT set has already made Packet
   * Monitor miss every data frame once. See the note in wifi.cpp. */
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
  esp_wifi_set_promiscuous_filter(&filt);

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_channel(channel, WIFI_SECOND_CHAN_NONE);
  esp_wifi_set_promiscuous_rx_cb(&onFrame);
  esp_wifi_set_promiscuous(true);
  s_promiscOn = true;
}

/* ── The list ─────────────────────────────────────────────────────────────── */

void sortByRssi() {
  for (int i = 1; i < s_count; i++) {
    const Ap key = s_aps[i];
    int j = i - 1;
    while (j >= 0 && s_aps[j].rssi < key.rssi) {
      s_aps[j + 1] = s_aps[j];
      j--;
    }
    s_aps[j + 1] = key;
  }
}

/* One scan, at entry. This is the only transmission the feature makes, and
 * it is the same call the scanner already makes; under Stealth Mode it is
 * passive, as it is everywhere else. */
void rescan() {
  s_count = 0;
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(50);

  const int n = WiFi.scanNetworks(false, true, Stealth::on(), 150);
  for (int i = 0; i < n && s_count < kMaxAps; i++) {
    Ap& a = s_aps[s_count];
    const uint8_t* b = WiFi.BSSID(i);
    if (!b) {
      continue;
    }
    memcpy(a.bssid, b, 6);
    const String ssid = WiFi.SSID(i);
    a.hidden = (ssid.length() == 0);
    snprintf(a.ssid, sizeof(a.ssid), "%s",
             a.hidden ? "(hidden)" : ssid.c_str());
    a.rssi    = (int8_t)WiFi.RSSI(i);
    a.channel = (uint8_t)WiFi.channel(i);
    s_count++;
  }
  WiFi.scanDelete();
  sortByRssi();
  if (s_sel >= s_count) {
    s_sel = s_count > 0 ? s_count - 1 : 0;
  }
}

void drawPicker() {
  const int top = 22;
  tft.fillRect(0, top, PUEO_SCREEN_W, contentBottom() - top, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(PUEO_BODY_SIZE);

  char hdr[40];
  snprintf(hdr, sizeof(hdr), "APs in range %d", s_count);
  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.drawString(hdr, 8, top + 2);

  if (s_count == 0) {
    tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
    tft.drawString("nothing heard.", 8, top + 24);
    tft.drawString("2.4 GHz only, so a 5 GHz", 8,
                   top + 24 + 12 * PUEO_BODY_SIZE);
    tft.drawString("AP will not appear.", 8,
                   top + 24 + 24 * PUEO_BODY_SIZE);
    return;
  }

  const int rows = (contentBottom() - (top + 18)) / kRowH;
  if (rows <= 0) {
    return;
  }
  if (s_sel < s_scroll) {
    s_scroll = s_sel;
  }
  if (s_sel >= s_scroll + rows) {
    s_scroll = s_sel - rows + 1;
  }

  int y = top + 18;
  for (int i = 0; i < rows && (s_scroll + i) < s_count; i++) {
    const Ap& a = s_aps[s_scroll + i];
    const bool sel = (s_scroll + i) == s_sel;

    if (sel) {
      tft.fillRect(0, y, PUEO_SCREEN_W, kRowH, 0x2124);
    }
    tft.setTextColor(sel ? ORANGE : TFT_WHITE, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(a.ssid, 8, y + 2);

    char mac[20];
    snprintf(mac, sizeof(mac), "%02X:%02X:%02X:%02X:%02X:%02X",
             a.bssid[0], a.bssid[1], a.bssid[2],
             a.bssid[3], a.bssid[4], a.bssid[5]);
    tft.setTextColor(sel ? ORANGE : TFT_DARKGREY, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(mac, 8, y + kRowLine2);

    /* Two lines and a size down. One line of "-45 dBm  ch11" wanted 156 px
     * starting 224 px into a 320 px panel, so the channel was never drawn.
     * TFT_eSPI does not clip; it just stops. */
    char dbm[12], ch[12];
    snprintf(dbm, sizeof(dbm), "%4d dBm", (int)a.rssi);
    snprintf(ch, sizeof(ch), "ch %2u", (unsigned)a.channel);
    tft.setTextSize(1);
    tft.setTextColor(sel ? ORANGE : TFT_WHITE, sel ? 0x2124 : TFT_BLACK);
    tft.drawString(dbm, kRightX, y + kRowRight);
    tft.drawString(ch, kRightX, y + kRowLine2 + 2);
    tft.setTextSize(PUEO_BODY_SIZE);

    y += kRowH;
  }
}

void drawGauge() {
  const bool lost = (uint32_t)(millis() - s_lockSeen) > kLostMs;

  char sub[20];
  snprintf(sub, sizeof(sub), "ch%u  %02X:%02X:%02X",
           (unsigned)s_lockChannel,
           s_lockBssid[3], s_lockBssid[4], s_lockBssid[5]);

  SignalGauge::draw(s_lockSsid, sub, s_lockHits, lost,
                    "out of range, or the AP",
                    "changed channel");
}

void enterPicker() {
  s_screen = Screen::Pick;
  s_locked = false;
  stopListening();
  s_dirty = true;
  /* "" not nullptr for up and down: a null slot draws the dots icon,
   * which is the bar saying that button does nothing, and in the
   * picker they scroll the list. An empty label skips the word and
   * draws the arrow. */
  setTouchNavLabels("Rescan", "", "Exit", "", "Track");
  redrawTouchButtonBar();
}

void enterGauge() {
  const Ap& a = s_aps[s_sel];
  memcpy(s_lockBssid, a.bssid, 6);
  snprintf(s_lockSsid, sizeof(s_lockSsid), "%s", a.ssid);
  s_lockChannel = a.channel;
  s_lockRssi    = a.rssi;
  s_lockSeen    = millis();
  s_lockHits    = 0;

  SignalGauge::reset();
  SignalGauge::sample(a.rssi);

  s_locked = true;
  startListening(a.channel);

  s_screen = Screen::Gauge;
  s_dirty  = true;
  setTouchNavLabels("List", nullptr, "Exit", nullptr, "Reset");
  redrawTouchButtonBar();
}

}  // namespace

void setup() {
  s_running = true;
  s_screen  = Screen::Pick;
  s_count   = 0;
  s_sel     = 0;
  s_scroll  = 0;
  s_dirty   = true;
  s_lastDraw = 0;
  s_locked  = false;
  s_promiscOn = false;
  memset(s_aps, 0, sizeof(s_aps));

  setTouchButtonInputEnabled(true);
  tft.fillScreen(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  enterPicker();
  rescan();
}

void loop() {
  if (!s_running) {
    return;
  }
  const uint32_t now = millis();

  if (s_screen == Screen::Pick) {
    if (isButtonPressed(BTN_UP)) {
      if (s_sel > 0) s_sel--;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_DOWN)) {
      if (s_sel + 1 < s_count) s_sel++;
      s_dirty = true;
      delay(120);
    } else if (isButtonPressed(BTN_LEFT)) {
      rescan();
      s_dirty = true;
      delay(200);
      waitForButtonRelease(BTN_LEFT);
    } else if (isButtonPressed(BTN_RIGHT)) {
      if (s_count > 0) {
        enterGauge();
      }
      delay(200);
      waitForButtonRelease(BTN_RIGHT);
    }
  } else {
    if (isButtonPressed(BTN_LEFT)) {
      enterPicker();
      rescan();
      delay(200);
      waitForButtonRelease(BTN_LEFT);
    } else if (isButtonPressed(BTN_RIGHT)) {
      SignalGauge::resetPeak((int8_t)s_lockRssi);
      s_dirty = true;
      delay(200);
      waitForButtonRelease(BTN_RIGHT);
    }

    /* Samples arrive on the Wi-Fi task. Fold them in here so the smoothing
     * runs at the redraw rate rather than at whatever rate the AP beacons,
     * which is the same reason Hunt samples in its draw. */
    SignalGauge::sample((int8_t)s_lockRssi);
    s_dirty = true;
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
  s_locked  = false;
  stopListening();

  /* Leave the filter as we found it for whoever runs next. Packet Monitor
   * wants data frames and has been starved of them once already by a
   * feature that set MASK_MGMT and walked away. */
  wifi_promiscuous_filter_t filt = {};
  filt.filter_mask = WIFI_PROMIS_FILTER_MASK_ALL;
  esp_wifi_set_promiscuous_filter(&filt);

  WiFi.scanDelete();
  requestStatusBarRedraw();
}

}  // namespace ApTracker
