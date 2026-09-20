#include "SettingsStore.h"
#include "Touchscreen.h"
#include "SpiBus.h"
#include <TFT_eSPI.h>

extern TFT_eSPI tft;

#if defined(BOARD_CYD) || defined(BOARD_ESP32_DIV_V1)
// Dedicated VSPI bus for XPT2046 — must not share HSPI with TFT_eSPI on classic ESP32.
SPIClass touchscreenSPI = SPIClass(VSPI);
#else
SPIClass touchscreenSPI = SPIClass(HSPI);
#endif

#ifndef XPT2046_IRQ
#define XPT2046_IRQ 255
#endif

XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
bool feature_active = false;

static bool s_touchInitialized = false;

#ifndef TOUCH_ROTATION
#if defined(BOARD_ESP32_DIV_V2)
#define TOUCH_ROTATION 0
#else
#define TOUCH_ROTATION TFT_ROTATION
#endif
#endif

#if TOUCH_SHARES_TFT_SPI
static void applyTouchRotation(int16_t rawX, int16_t rawY, int16_t& x, int16_t& y) {
  switch (TOUCH_ROTATION) {
    case 0: x = 4095 - rawY; y = rawX; break;
    case 1: x = rawX; y = rawY; break;
    case 2: x = rawY; y = 4095 - rawX; break;
    default: x = 4095 - rawX; y = 4095 - rawY; break;
  }
}

static bool readSharedTouchSample(int16_t& x, int16_t& y, int16_t& z, uint16_t zThresh) {
  if (!s_touchInitialized) {
    return false;
  }

  tft.endWrite();
  z = (int16_t)tft.getTouchRawZ();
  if (z < (int16_t)zThresh) {
    x = 0;
    y = 0;
    return false;
  }

  uint16_t rawX = 0;
  uint16_t rawY = 0;
  tft.getTouchRaw(&rawX, &rawY);
  applyTouchRotation((int16_t)rawX, (int16_t)rawY, x, y);
  return true;
}
#endif

static void ensureTouchSpiReady() {
#if !TOUCH_SHARES_TFT_SPI
  // This begin() only does anything the first time. SPIClass::begin() returns
  // immediately once _spi is set, so it cannot re-attach the pins after a
  // radio or the SD card has pointed the bus elsewhere -- which is exactly
  // what used to happen, and why touch stopped responding after the first
  // feature that touched the shared bus.
  touchscreenSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  if (SpiBus::touchSharesRadioBus()) {
    // Cheap no-op while touch still holds the bus; re-points the GPIO matrix
    // back at T_CLK/T_DIN/T_OUT when something else took it.
    SpiBus::claim(SpiBus::Dev::Touch);
  }
#endif
}

/* TEMPORARY bring-up diagnostic -- remove before committing. Reports what
 * the controller is actually doing, so a dead panel can be told apart from
 * a wrong IRQ pin and from a wrong SPI bus. */
#ifndef PUEO_TOUCH_DEBUG
#define PUEO_TOUCH_DEBUG 0
#endif

static bool touchSampleOk(uint16_t zThresh, int16_t& rawX, int16_t& rawY) {
#if PUEO_TOUCH_DEBUG && !TOUCH_SHARES_TFT_SPI
  {
    static uint32_t s_lastDump = 0;
    if (millis() - s_lastDump > 400) {
      s_lastDump = millis();
      ensureTouchSpiReady();
      TS_Point dp = ts.getPoint();
      int irq = -1;
#if defined(XPT2046_IRQ) && (XPT2046_IRQ < 255)
      pinMode(XPT2046_IRQ, INPUT);
      irq = digitalRead(XPT2046_IRQ);
#endif
      Serial.printf("[touch] irq(%d)=%d tirq=%d touched=%d raw x=%d y=%d z=%d\n",
                    (int)XPT2046_IRQ, irq, (int)ts.tirqTouched(), (int)ts.touched(),
                    (int)dp.x, (int)dp.y, (int)dp.z);
    }
  }
#endif
#if TOUCH_SHARES_TFT_SPI
#if PUEO_TOUCH_DEBUG
  {
    static uint32_t s_lastDump2 = 0;
    if (millis() - s_lastDump2 > 70) {
      s_lastDump2 = millis();
      tft.endWrite();
      uint16_t dx = 0, dy = 0;
      int16_t dz = (int16_t)tft.getTouchRawZ();
      tft.getTouchRaw(&dx, &dy);
      Serial.printf("[touch] shared raw x=%d y=%d z=%d\n",
                    (int)dx, (int)dy, (int)dz);
    }
  }
#endif
  int16_t z = 0;
  return readSharedTouchSample(rawX, rawY, z, zThresh);
#else
  ensureTouchSpiReady();
#if defined(XPT2046_IRQ) && (XPT2046_IRQ < 255)
  if (!ts.tirqTouched()) {
    return false;
  }
#endif
  if (!ts.touched()) {
    return false;
  }
  TS_Point p = ts.getPoint();
  if (p.z < (int16_t)zThresh) {
    return false;
  }
  rawX = p.x;
  rawY = p.y;
  return true;
#endif
}

void setupTouchscreen() {
  if (s_touchInitialized) {
    return;
  }

#if TOUCH_SHARES_TFT_SPI
  pinMode(XPT2046_CS, OUTPUT);
  digitalWrite(XPT2046_CS, HIGH);
#else
  ensureTouchSpiReady();
  ts.begin(touchscreenSPI);
  ts.setRotation(TOUCH_ROTATION);
#endif

  s_touchInitialized = true;
}

extern XPT2046_Touchscreen ts;

bool isTouchDown(uint16_t zThresh) {
  int16_t x = 0;
  int16_t y = 0;
  return touchSampleOk(zThresh, x, y);
}

bool isTouchDownDismiss(uint16_t zThresh) {
  return isTouchDown(zThresh);
}

bool readTouchRawXY(int16_t& x, int16_t& y, uint16_t zThresh) {
  return touchSampleOk(zThresh, x, y);
}

static void mapTouchToScreen(int16_t rawX, int16_t rawY, int& x, int& y) {
  auto& s = settings();
  x = ::map(rawX, s.touchXMin, s.touchXMax, 0, TFT_WIDTH - 1);
#if TOUCH_INVERT_Y
  /* Y runs the other way on this panel: raw falls as the screen
   * coordinate rises, measured at -0.96 correlation. */
  y = ::map(rawY, s.touchYMax, s.touchYMin, 0, TFT_HEIGHT - 1);
#else
  // Same axis order as the RNT CYD touch test (no inverted Y).
  y = ::map(rawY, s.touchYMin, s.touchYMax, 0, TFT_HEIGHT - 1);
#endif
}

bool readTouchXY(int& x, int& y) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(200, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}

bool readTouchXYDismiss(int& x, int& y) {
  int16_t rawX = 0;
  int16_t rawY = 0;
  if (!touchSampleOk(120, rawX, rawY)) {
    return false;
  }
  mapTouchToScreen(rawX, rawY, x, y);
  return true;
}
