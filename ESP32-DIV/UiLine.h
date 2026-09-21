#pragma once
/* One line of text that repaints only when it changes.
 *
 * Five screens have now had the same fault: clear the area, redraw every
 * row, several times a second. It is invisible while the area is empty --
 * which is how every one of them was written and tested -- and it is a
 * black flash behind every row the moment anything is actually there.
 * Hunt, Surveillance, Fast Pair and Drones had it, and then so did the
 * bench beacon, which was written while the other four were being fixed.
 *
 * It is in a header rather than in utils.cpp because the beacon is a
 * separate sketch and does not compile utils.cpp. A second copy over there
 * would have been the sixth.
 *
 * `shown` is the caller's record of what this line last said; the caller
 * owns the storage, one buffer per line slot. Returns true if it repainted,
 * which a caller needs when clearing a band also wipes something drawn
 * beside it. `h` is the height to clear -- a replacement can be shorter
 * than what it replaces, and drawString only paints the glyphs it draws.
 *
 * Note what it does NOT compare: colour. A line whose text is unchanged but
 * whose colour should change will not repaint. Put the state in the string
 * as well -- Surveillance's DWELL marker is there for that reason.
 */

#include <TFT_eSPI.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

extern TFT_eSPI tft;

#ifndef PUEO_UI_LINE_WIDTH
#define PUEO_UI_LINE_WIDTH PUEO_SCREEN_W
#endif

inline bool uiShowLine(char* shown, size_t shownSz, const char* text,
                       int x, int y, int h, uint16_t fg, uint16_t bg) {
  if (strncmp(shown, text, shownSz - 1) == 0) {
    return false;
  }
  tft.fillRect(0, y, PUEO_UI_LINE_WIDTH, h, bg);
  tft.setTextColor(fg, bg);
  tft.drawString(text, x, y);
  snprintf(shown, shownSz, "%s", text);
  return true;
}
