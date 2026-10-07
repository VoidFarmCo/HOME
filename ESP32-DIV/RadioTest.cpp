#include "shared.h"
#include "utils.h"
#include "Nrf24Raw.h"

/* The bounded, bus-safe CC1101 presence check from subghz.cpp. NOT the ELECHOUSE
 * library: its Init()/getCC1101()/SpiReadStatus() all open with an unbounded
 * `while (digitalRead(MISO));` that hangs forever when no module is wired, which
 * on a bare board froze this feature before its loop could run (you could not
 * exit). subghzCc1101Present() reads PARTNUM/VERSION with a deadline instead. */
bool subghzCc1101Present();

namespace RadioTest {

static constexpr uint16_t C_OK   = 0x07E0;   // green
static constexpr uint16_t C_NO   = 0xF800;   // red
static constexpr uint16_t C_TEXT = 0xFFFF;   // white

static bool probeCC1101() {
  return subghzCc1101Present();   // bounded register read; never hangs on a bare board
}

static bool probeNrf24() {
  Nrf24Raw::begin();
  const bool p = Nrf24Raw::present();
  Nrf24Raw::stop();
  return p;
}

static void row(int& y, const char* name, const char* pins, bool present) {
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.fillRect(0, y, PUEO_SCREEN_W, 16, TFT_BLACK);
  tft.setTextColor(C_TEXT, TFT_BLACK);
  tft.setCursor(8, y);
  tft.print(name);
  tft.setTextColor(present ? C_OK : C_NO, TFT_BLACK);
  tft.setCursor(118, y);
  tft.print(present ? "present" : "not found");
  tft.setTextColor(0x8410, TFT_BLACK);   // grey
  tft.setCursor(200, y);
  tft.print(pins);
  y += 18;
}

static void runProbes() {
  const int bottom = featureHasTouchNavBar() ? (int)touchNavContentBottomY() : 300;
  tft.fillRect(0, 38, PUEO_SCREEN_W, bottom - 38, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_ACCENT, TFT_BLACK);
  tft.setCursor(8, 42);
  tft.print("Radio Test");

  int y = 64;
  row(y, "SD card", "CS5",       isSDCardAvailable());
  row(y, "CC1101",  "CS21 G22/35", probeCC1101());
  row(y, "NRF24",   "CE16 CSN25",  probeNrf24());

  tft.setTextColor(0x8410, TFT_BLACK);
  tft.setCursor(8, y + 6);
  tft.print("Rescan to re-probe.");
}

void setup() {
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setTouchNavLabels("Rescan", nullptr, "Exit", nullptr, nullptr);
  redrawTouchButtonBar();
  setupTouchscreen();
  runProbes();
}

void loop() {
  if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isButtonPressed(BTN_LEFT)) {   // "Rescan"
    delay(200);
    runProbes();
    return;
  }
  delay(20);
}

}  // namespace RadioTest
