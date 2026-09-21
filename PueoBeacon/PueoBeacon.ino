/* ─────────────────────────────────────────────────────────────────────────────
 * Pueo Beacon — the bench transmitter that proves Pueo's receivers work.
 *
 * Pueo's detectors are checked against synthetic frames and, where one
 * exists, a reference implementation. None of that proves a radio path: a
 * decoder can be correct while the scan never hands it a byte, and the
 * symptom is an empty list, which is also what an empty room looks like.
 *
 * This is the other half. Flash it to a second board, put the two side by
 * side, and every detector in Pueo has something to find.
 *
 *   Drones     Remote ID over Wi-Fi Beacon and BLE 4 legacy
 *   Spotter    a plate-reader probe, a body-camera beacon, smart glasses,
 *              a vehicle module
 *   Hunt       a Find My tracker advertisement
 *   Fast Pair  both payload shapes, discoverable and not
 *
 * Build and flash:
 *
 *   PUEO_ROLE=beacon tools/build.sh
 *   PUEO_ROLE=beacon tools/build.sh upload COM5
 *
 * It is a separate image from the detector on purpose. See Emit.h.
 *
 * Everything it sends is built to be recognised as a decoy rather than to be
 * convincing: PUEO-TEST identifiers, DEAD in the device half of every
 * address, transmit power at the floor, and a ten-minute auto-stop. A test
 * kit you cannot tell from the real article is a worse test kit, not a
 * better one.
 * ────────────────────────────────────────────────────────────────────────── */

#include "Emit.h"

#include <TFT_eSPI.h>

/* The detector's own headers. shared.h carries the panel geometry, the
 * backlight pin and the rotation, all of which move with PUEO_PANEL -- so
 * the two images cannot disagree about the board they are on. */
#include "Branding.h"
#include "shared.h"

TFT_eSPI tft = TFT_eSPI();

namespace {

constexpr uint16_t kBg     = 0x0000;
constexpr uint16_t kText   = 0xFFFF;
constexpr uint16_t kDim    = 0x8410;
constexpr uint16_t kLive   = 0x07E0;
constexpr uint16_t kWarn   = 0xFBE0;
constexpr uint16_t kStop   = 0xF800;

/* Wi-Fi signals are events and go out every kWifiMs. BLE advertising is a
 * state, so only one BLE signal can be live at a time and they take turns
 * for kBleSliceMs each. */
constexpr uint32_t kWifiMs     = 250;
constexpr uint32_t kBleSliceMs = 3000;
constexpr uint32_t kDrawMs     = 400;

uint32_t s_started   = 0;
uint32_t s_lastWifi  = 0;
uint32_t s_lastSlice = 0;
uint32_t s_lastDraw  = 0;
bool     s_running   = true;
uint8_t  s_bleIdx    = 0;

const Emit::Signal kWifiSignals[] = {
  Emit::RemoteIdWifi, Emit::AlprProbe, Emit::BodycamBeacon,
};
const Emit::Signal kBleSignals[] = {
  Emit::RemoteIdBle, Emit::GlassesBle, Emit::VehicleBle,
  Emit::TrackerBle, Emit::FastPairBle,
};
constexpr uint8_t kWifiCount = sizeof(kWifiSignals) / sizeof(kWifiSignals[0]);
constexpr uint8_t kBleCount  = sizeof(kBleSignals) / sizeof(kBleSignals[0]);

uint8_t s_wifiIdx = 0;

void drawFrame() {
  tft.fillScreen(kBg);
  tft.setTextFont(2);
  tft.setTextColor(kWarn, kBg);
  tft.drawString("PUEO BEACON", 8, 6);
  tft.setTextFont(1);
  tft.setTextColor(kDim, kBg);
  tft.drawString("bench transmitter - " PUEO_VERSION, 8, 28);
  tft.drawFastHLine(0, 42, PUEO_SCREEN_W, kDim);
}

void drawBody() {
  const int top = 50;
  tft.fillRect(0, top, PUEO_SCREEN_W, PUEO_SCREEN_H - top, kBg);
  tft.setTextFont(1);

  int y = top;
  if (!s_running) {
    tft.setTextColor(kStop, kBg);
    tft.drawString("STOPPED - auto-stop after 10 min", 8, y);
    tft.setTextColor(kDim, kBg);
    tft.drawString("reset the board to transmit again", 8, y + 12);
    y += 32;
  } else {
    tft.setTextColor(kLive, kBg);
    const uint32_t leftS = (Emit::kAutoStopMs - (millis() - s_started)) / 1000u;
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "TRANSMITTING - stops in %lu:%02lu",
             (unsigned long)(leftS / 60), (unsigned long)(leftS % 60));
    tft.drawString(hdr, 8, y);
    y += 16;
  }

  const Emit::Signal live = Emit::currentBle();
  for (uint8_t i = 0; i < Emit::kSignalCount; i++) {
    const Emit::Signal s = (Emit::Signal)i;
    const uint32_t n = Emit::sentCount(s);
    const bool isLiveBle = (s == live);

    tft.setTextColor(isLiveBle ? kLive : (n ? kText : kDim), kBg);
    tft.drawString(Emit::name(s), 8, y);

    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)n);
    tft.drawString(cnt, PUEO_SCREEN_W - 44, y);

    tft.setTextColor(kDim, kBg);
    tft.drawString(Emit::detectedBy(s), 20, y + 10);
    y += 24;
  }

  tft.setTextColor(kDim, kBg);
  tft.drawString("all payloads say PUEO-TEST", 8, PUEO_SCREEN_H - 14);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[beacon] start");

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(TFT_ROTATION);
  drawFrame();

  Emit::begin();
  s_started = millis();
  drawBody();
}

void loop() {
  const uint32_t now = millis();

  if (s_running && (now - s_started) >= Emit::kAutoStopMs) {
    /* Stops on its own. A bench unit left powered should not still be
     * shouting an hour later because nobody walked back to it. */
    Emit::allStop();
    s_running = false;
    drawBody();
  }

  if (s_running) {
    if ((uint32_t)(now - s_lastWifi) >= kWifiMs) {
      s_lastWifi = now;
      Emit::send(kWifiSignals[s_wifiIdx]);
      s_wifiIdx = (uint8_t)((s_wifiIdx + 1) % kWifiCount);
    }

    if ((uint32_t)(now - s_lastSlice) >= kBleSliceMs) {
      s_lastSlice = now;
      Emit::send(kBleSignals[s_bleIdx]);
      s_bleIdx = (uint8_t)((s_bleIdx + 1) % kBleCount);
    }
  }

  if ((uint32_t)(now - s_lastDraw) >= kDrawMs) {
    s_lastDraw = now;
    drawBody();
  }

  delay(4);
}
