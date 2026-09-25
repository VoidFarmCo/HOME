#include "SignalGauge.h"

#include "config.h"
#include "shared.h"
#include "utils.h"

#include <math.h>
#include <stdio.h>
#include <string.h>

namespace SignalGauge {
namespace {

/* How hard the smoothing pulls. 0.25 is four readings to most of the way,
 * which is slow enough to read while walking and fast enough that turning
 * round shows up. */
constexpr float kAlpha = 0.25f;

/* The trend is measured over a window rather than per sample, because the
 * sample rate is the caller's business: a BLE tracker advertises when it
 * likes and a Wi-Fi AP beacons about ten times a second. A window means the
 * word on screen changes at the same rate either way. */
constexpr uint32_t kTrendWindowMs = 1200;
constexpr float    kTrendDeadband = 2.0f;

float    s_smooth = (float)kRssiFar;
int8_t   s_peak   = kRssiFar;

float    s_trendBase = (float)kRssiFar;
uint32_t s_trendAt   = 0;
int      s_trend     = 0;          // +1 warmer, -1 colder, 0 hold

bool     s_chrome    = false;
int      s_prevAngle = -1;

/* What the readout last drew, so a frame that changed nothing draws
 * nothing. */
char     s_shownTrend[16] = {0};
char     s_shownBand[24]  = {0};
char     s_shownNums[52]  = {0};
char     s_shownNums2[52] = {0};
int      s_shownFill      = -1;
int      s_shownPeakPx    = -1;
int8_t   s_shownLost      = -1;

struct Dial {
  int cx, cy, r;
};

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

Dial dial() {
  const int top    = 30;
  const int bottom = contentBottom();
  Dial d;
  d.cx = PUEO_SCREEN_W / 2;
  /* The arc is the top half of a circle, so it needs r of height, and the
   * readout below needs about 90: trend word, band, bar and the numbers.
   * Whichever of width and height runs out first sets the radius, which on
   * both panels is the width. The reserve is stated so it stays true if the
   * readout grows again. */
  const int byWidth  = PUEO_SCREEN_W / 2 - 10;
  const int byHeight = (bottom - top - 90);
  d.r  = byWidth < byHeight ? byWidth : byHeight;
  d.cy = top + d.r;
  return d;
}

/* RSSI to needle angle in degrees: 180 is hard left and far, 0 is hard
 * right and near. */
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

void drawChrome(const Dial& d, const char* title, const char* sub) {
  tft.fillRect(0, 22, PUEO_SCREEN_W, contentBottom() - 22, TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  tft.setTextColor(ORANGE, TFT_BLACK);
  tft.drawString(title, 8, 24);

  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString(sub, PUEO_SCREEN_W - 104, 24);

  drawArc(d);
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.drawString("far", d.cx - d.r, d.cy + 2);
  tft.drawString("near", d.cx + d.r - 22, d.cy + 2);
  tft.fillCircle(d.cx, d.cy, 3, TFT_DARKGREY);
}

/* Redraw one centred line only if the text changed, clearing just its own
 * band first. drawCentreString paints a background behind each glyph, but a
 * shorter string leaves the tail of the longer one behind, so the clear has
 * to happen. Just not over the whole readout, and not when nothing moved. */
void showLine(char* shown, size_t shownSz, const char* text,
              int cx, int y, uint8_t size, uint16_t colour, bool force) {
  if (!force && strncmp(shown, text, shownSz - 1) == 0) {
    return;
  }
  tft.fillRect(0, y, PUEO_SCREEN_W, 8 * size, TFT_BLACK);
  tft.setTextSize(size);
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawCentreString(text, cx, y, 1);
  snprintf(shown, shownSz, "%s", text);
}

}  // namespace

float smoothed() { return s_smooth; }
int8_t peak()    { return s_peak; }

void resetPeak(int8_t to) {
  s_peak = to;
  /* The needle and the marker are drawn together, so the next frame has to
   * redraw them even if the angle has not moved. */
  s_prevAngle = -1;
}

void reset() {
  s_smooth    = (float)kRssiFar;
  s_peak      = kRssiFar;
  s_trendBase = s_smooth;
  s_trendAt   = millis();
  s_trend     = 0;
  s_chrome    = false;
  s_prevAngle = -1;
  /* The readout caches what it has drawn and draw() is about to clear the
   * screen, so the cache has to stop agreeing with it. -1 makes the next
   * frame cross and redraw the lot. */
  s_shownLost   = -1;
  s_shownFill   = -1;
  s_shownPeakPx = -1;
  s_shownTrend[0] = s_shownBand[0] = s_shownNums[0] = s_shownNums2[0] = '\0';
}

void sample(int8_t rssi) {
  /* First reading after a reset starts the average at the value rather than
   * crawling up from the floor, which otherwise shows a needle climbing for
   * a second that has nothing to do with the room. */
  if (s_peak == kRssiFar && s_smooth == (float)kRssiFar) {
    s_smooth    = (float)rssi;
    s_trendBase = s_smooth;
  } else {
    s_smooth = s_smooth * (1.0f - kAlpha) + (float)rssi * kAlpha;
  }
  if (rssi > s_peak) {
    s_peak = rssi;
  }
}

void draw(const char* title, const char* sub, uint32_t hits,
          bool lost, const char* hint1, const char* hint2) {
  const Dial d = dial();
  const uint32_t now = millis();

  if (!s_chrome) {
    drawChrome(d, title, sub);
    s_chrome = true;
    s_prevAngle = -1;
  }

  if (!lost && (uint32_t)(now - s_trendAt) >= kTrendWindowMs) {
    const float delta = s_smooth - s_trendBase;
    s_trend = (delta > kTrendDeadband) ? 1 : (delta < -kTrendDeadband ? -1 : 0);
    s_trendBase = s_smooth;
    s_trendAt   = now;
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

  /* The readout, in the order it is useful. A needle alone says where you
   * are and not what to do about it. The technique is to move and watch the
   * direction, so the direction is the biggest thing on the screen and the
   * number is the smallest. */
  const int ty = d.cy + 14;
  tft.setTextFont(1);

  /* Crossing between lost and found changes the shape of the readout, so
   * that transition clears it once. Staying in either state does not. */
  const bool crossed = (s_shownLost != (int8_t)(lost ? 1 : 0));
  if (crossed) {
    tft.fillRect(0, ty, PUEO_SCREEN_W, contentBottom() - ty, TFT_BLACK);
    s_shownTrend[0] = s_shownBand[0] = s_shownNums[0] = '\0';
    s_shownNums2[0] = '\0';
    s_shownFill = s_shownPeakPx = -1;
    s_shownLost = lost ? 1 : 0;
  }

  if (lost) {
    showLine(s_shownTrend, sizeof(s_shownTrend), "LOST",
             d.cx, ty, 3, TFT_DARKGREY, crossed);
    showLine(s_shownBand, sizeof(s_shownBand), hint1,
             d.cx, ty + 30, PUEO_BODY_SIZE, TFT_DARKGREY, crossed);
    showLine(s_shownNums, sizeof(s_shownNums), hint2,
             d.cx, ty + 30 + 12 * PUEO_BODY_SIZE, PUEO_BODY_SIZE,
             TFT_DARKGREY, crossed);
    showLine(s_shownNums2, sizeof(s_shownNums2), "Exit and re-pick",
             d.cx, ty + 30 + 24 * PUEO_BODY_SIZE, PUEO_BODY_SIZE,
             TFT_DARKGREY, crossed);
    return;
  }

  const char* trendWord = "HOLD";
  uint16_t    trendCol  = TFT_DARKGREY;
  if (s_trend > 0) {
    trendWord = "WARMER";
    trendCol  = TFT_GREEN;
  } else if (s_trend < 0) {
    trendWord = "COLDER";
    trendCol  = TFT_BLUE;
  }
  showLine(s_shownTrend, sizeof(s_shownTrend), trendWord,
           d.cx, ty, 3, trendCol, crossed);

  /* A coarse band, in words. Not metres: see the header. The thresholds are
   * where the needle sits, not where the target is, and the last one says
   * "arm's length" rather than a number because that is the honest claim. */
  const int sm = (int)(s_smooth - 0.5f);
  const char* band;
  uint16_t bandCol;
  if (sm < -85)      { band = "FAR";          bandCol = TFT_DARKGREY; }
  else if (sm < -70) { band = "CLOSER";       bandCol = TFT_WHITE;    }
  else if (sm < -55) { band = "NEAR";         bandCol = ORANGE;       }
  else if (sm < -45) { band = "VERY CLOSE";   bandCol = ORANGE;       }
  else               { band = "ARM'S LENGTH"; bandCol = TFT_RED;      }
  showLine(s_shownBand, sizeof(s_shownBand), band,
           d.cx, ty + 30, 2, bandCol, crossed);

  /* Strength bar. The same value as the needle, in the shape people read
   * signal from, because a bar filling is easier to catch out of the corner
   * of an eye than a needle rotating. */
  const int bw  = PUEO_SCREEN_W - 40;
  const int bx  = 20;
  const int by  = ty + 54;
  const int inner = bw - 2;                 // between the border's own pixels
  int fill = (int)(((float)(sm - kRssiFar) /
                    (float)(kRssiNear - kRssiFar)) * (float)inner);
  if (fill < 0)     fill = 0;
  if (fill > inner) fill = inner;

  int px = -1;
  if (s_peak > kRssiFar) {
    px = bx + 1 + (int)(((float)(s_peak - kRssiFar) /
                         (float)(kRssiNear - kRssiFar)) * (float)inner);
    if (px < bx + 1)         px = bx + 1;
    if (px > bx + 1 + inner) px = bx + 1 + inner;
  }

  /* The bar only redraws when it moves, and when it does it paints the whole
   * interior: fill, then the remainder in black. The remainder is the part
   * that was once missing, so a bar that had been long never got shorter and
   * walking away left it where the closest approach had put it. */
  if (crossed || fill != s_shownFill || px != s_shownPeakPx) {
    tft.drawRect(bx, by, bw, 10, TFT_DARKGREY);
    tft.fillRect(bx + 1, by + 1, fill, 8, bandCol);
    if (fill < inner) {
      tft.fillRect(bx + 1 + fill, by + 1, inner - fill, 8, TFT_BLACK);
    }
    if (px >= 0) {
      tft.drawFastVLine(px, by - 3, 16, TFT_GREEN);
    }
    s_shownFill   = fill;
    s_shownPeakPx = px;
  }

  /* Two lines, because one does not fit. At PUEO_BODY_SIZE on the 3.5" a
   * font-1 glyph advances 12 px, so 320 px holds 26 characters and a single
   * line of this ran off both edges of the panel. */
  char line[52];
  snprintf(line, sizeof(line), "%d dBm    best %d", sm, (int)s_peak);
  showLine(s_shownNums, sizeof(s_shownNums), line,
           d.cx, by + 18, PUEO_BODY_SIZE, TFT_DARKGREY, crossed);
  snprintf(line, sizeof(line), "%lu seen", (unsigned long)hits);
  showLine(s_shownNums2, sizeof(s_shownNums2), line,
           d.cx, by + 18 + 12 * PUEO_BODY_SIZE, PUEO_BODY_SIZE,
           TFT_DARKGREY, crossed);
}

}  // namespace SignalGauge
