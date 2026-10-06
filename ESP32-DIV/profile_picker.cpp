#include <Arduino.h>
#include <TFT_eSPI.h>
#include "profile_picker.h"
#include "home_ui.h"
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "utils.h"
#include "shared.h"
#include "icon.h"

extern TFT_eSPI tft;

/* The picker is its own H.O.M.E screen: two full-height tiles, each in that
 * profile's own accent with its own mark, so the choice looks like what it turns
 * the device into. No prompt copy -- the tiles say it. Colours come only from the
 * kit/palette (accentColor565, UI_*), which check_ui_kit.py holds us to. */
namespace ProfilePicker {

namespace {

struct Tile {
  int x, y, w, h;
  Profile profile;
};

bool hit(const Tile& t, int x, int y) {
  return x >= t.x && x <= t.x + t.w && y >= t.y && y <= t.y + t.h;
}

/* A plain house mark drawn from primitives -- roof, body, a door cut in the
 * tile's own accent so it reads as a doorway, not a sticker. No asset needed. */
void drawHouse(int cx, int top, uint16_t doorColour) {
  const int bodyW = 48, bodyH = 34;
  const int bodyY = top + 26;
  tft.fillRect(cx - bodyW / 2, bodyY, bodyW, bodyH, TFT_WHITE);
  tft.fillTriangle(cx - 34, bodyY, cx + 34, bodyY, cx, top, TFT_WHITE);
  tft.fillRect(cx - 7, bodyY + bodyH - 20, 14, 20, doorColour);
}

void drawTile(const Tile& t) {
  const uint16_t accent = accentColor565(homeUiDefaultAccent(t.profile));
  tft.fillRoundRect(t.x, t.y, t.w, t.h, HOME_UI_TILE_RADIUS, accent);
  tft.drawRoundRect(t.x, t.y, t.w, t.h, HOME_UI_TILE_RADIUS, TFT_WHITE);

  const int cx = t.x + t.w / 2;
  const int cy = t.y + t.h / 2;
  const int artTop = cy - 60;

  if (t.profile == Profile::Combat) {
    /* The crowned-skull mark, the brand's own, scaled up from the 16x16 icon. */
    drawBitmapScaled(cx - 32, artTop, bitmap_icon_skull, 16, 16, TFT_WHITE, 4);
  } else {
    drawHouse(cx, artTop, accent);
  }

  tft.setTextDatum(MC_DATUM);
  tft.setTextColor(TFT_WHITE, accent);
  const char* tag   = homeUiProfileTag(t.profile);    /* HOME / COMBAT        */
  const char* title = homeUiProfileTitle(t.profile);  /* full name, if different */
  tft.drawString(tag, cx, cy + 26, 2);
  /* Only draw the title line when it says something the tag does not -- for
   * HOME the two are both "HOME", and printing it twice is the repeat. */
  if (strcmp(tag, title) != 0) {
    tft.drawString(title, cx, cy + 54, 2);
  }
}

}  // namespace

void run() {
  const int W = tft.width();
  const int H = tft.height();
  const int pad = HOME_UI_PAD;
  const int gap = HOME_UI_TILE_GAP;
  const int tileW = W - 2 * pad;
  const int tileH = (H - 2 * pad - gap) / 2;

  Tile home { pad, pad, tileW, tileH, Profile::Home };
  Tile comb { pad, pad + tileH + gap, tileW, tileH, Profile::Combat };

  tft.fillScreen(UI_BG);
  drawTile(home);
  drawTile(comb);

  for (;;) {
    int x = 0, y = 0;
    if (readTouchXY(x, y)) {
      Profile chosen;
      bool picked = false;
      if (hit(home, x, y)) { chosen = Profile::Home;   picked = true; }
      else if (hit(comb, x, y)) { chosen = Profile::Combat; picked = true; }

      if (picked) {
        /* Wait for the finger to lift so the tap does not fall through onto
         * whatever is drawn next. */
        while (isTouchDown()) delay(10);

        auto& s = settings();
        s.profile = chosen;
        s.profileChosen = true;
        AppSettingsUI::applyAccent(homeUiDefaultAccent(chosen));
        settingsSave();
        return;
      }
    }
    delay(15);
  }
}

}  // namespace ProfilePicker
