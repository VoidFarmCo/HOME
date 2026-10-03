#include "shared.h"
#include "utils.h"
#include "Branding.h"

namespace SysInfo {

static int s_dynY = 120;          // where the live (heap/uptime) lines start
static uint32_t s_lastRefresh = 0;

static void printRow(int& y, uint16_t color, const char* text) {
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(10, y);
  tft.print(text);
  y += 18;
}

/* Heap-free and uptime change, so they are redrawn on their own clock while
 * everything above them is drawn once in setup(). */
static void drawDynamic() {
  char line[48];
  tft.setTextFont(1);
  tft.setTextSize(1);
  int y = s_dynY;
  tft.fillRect(0, y, PUEO_SCREEN_W, 40, TFT_BLACK);
  snprintf(line, sizeof(line), "RAM:   %lu / %lu KB free",
           (unsigned long)(ESP.getFreeHeap() / 1024),
           (unsigned long)(ESP.getHeapSize() / 1024));
  printRow(y, UI_TEXT, line);
  const uint32_t up = millis() / 1000;
  snprintf(line, sizeof(line), "Uptime: %lum %lus",
           (unsigned long)(up / 60), (unsigned long)(up % 60));
  printRow(y, UI_TEXT, line);
}

void setup() {
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setTouchNavLabels(nullptr, nullptr, "Exit", nullptr, nullptr);
  redrawTouchButtonBar();

  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextDatum(TL_DATUM);

  int y = 40;
  printRow(y, UI_ACCENT, "Device Info");
  y += 4;

  char line[56];
  snprintf(line, sizeof(line), "FW:     %s %s", PUEO_NAME, PUEO_VERSION);
  printRow(y, UI_TEXT, line);
  snprintf(line, sizeof(line), "Board:  %s", ESP32DIV_BOARD_NAME);
  printRow(y, UI_TEXT, line);
  snprintf(line, sizeof(line), "Chip:   %s x%d %dMHz",
           ESP.getChipModel(), (int)ESP.getChipCores(), (int)ESP.getCpuFreqMHz());
  printRow(y, UI_TEXT, line);
  snprintf(line, sizeof(line), "Flash:  %lu MB",
           (unsigned long)(ESP.getFlashChipSize() / (1024UL * 1024UL)));
  printRow(y, UI_TEXT, line);
  const uint32_t psram = ESP.getPsramSize();
  if (psram) {
    snprintf(line, sizeof(line), "PSRAM:  %lu MB", (unsigned long)(psram / (1024UL * 1024UL)));
  } else {
    snprintf(line, sizeof(line), "PSRAM:  none");
  }
  printRow(y, UI_TEXT, line);
  const uint64_t id = ESP.getEfuseMac();
  snprintf(line, sizeof(line), "ID:     %04X%08lX",
           (unsigned)(uint16_t)(id >> 32), (unsigned long)(uint32_t)id);
  printRow(y, UI_TEXT, line);
  snprintf(line, sizeof(line), "SD:     %s", isSDCardAvailable() ? "present" : "none");
  printRow(y, UI_TEXT, line);

  s_dynY = y;
  s_lastRefresh = 0;
  drawDynamic();
}

void loop() {
  if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  const uint32_t now = millis();
  if (now - s_lastRefresh >= 1000) {
    s_lastRefresh = now;
    drawDynamic();
  }
  delay(20);
}

}  // namespace SysInfo
