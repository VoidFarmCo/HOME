#include "Status.h"
#include <Arduino.h>
#include "shared.h"
#include "utils.h"
#include "Touchscreen.h"

namespace Status {

void explain(const char* what, const char* why, const char* fix) {
  const uint16_t accent = homeAccent();
  tft.fillScreen(UI_BG);

  // Headline in the live accent, with the H.O.M.E accent rule under it.
  tft.setTextFont(2);
  tft.setTextSize(1);
  tft.setTextColor(accent, UI_BG);
  tft.setCursor(8, 8);
  tft.print(what ? what : "");
  tft.fillRect(8, 28, tft.width() - 16, 2, accent);

  // Why, in plain words, wrapped to the screen.
  tft.setTextFont(1);
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setTextWrap(true);
  tft.setCursor(8, 40);
  tft.print(why ? why : "");

  // What to do.
  if (fix && fix[0]) {
    tft.setTextColor(accent, UI_BG);
    tft.setCursor(8, 150);
    tft.print("Try: ");
    tft.setTextColor(UI_TEXT, UI_BG);
    tft.print(fix);
  }
  tft.setTextWrap(false);

  // Dismiss hint along the bottom.
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(8, tft.height() - 16);
  tft.print("Tap or press a key to continue");

  // Block until the user acknowledges. The lead-in delay swallows the press
  // that may have opened this panel so it does not dismiss instantly.
  delay(250);
  for (;;) {
    int x = 0, y = 0;
    if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed() || readTouchXY(x, y)) {
      break;
    }
    delay(20);
  }
  delay(150);
}

}  // namespace Status
