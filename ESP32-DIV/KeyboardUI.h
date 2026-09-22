#ifndef KEYBOARD_UI_H
#define KEYBOARD_UI_H

#include <Arduino.h>

/** Rows in the standard on-screen layout (set cfg.rows = nullptr to use it). */
#define OS_KEYBOARD_ROW_COUNT 4

struct OnScreenKeyboardConfig {

  const char* titleLine1;
  const char* titleLine2;

  /** When null, the built-in standard layout (lower / upper / symbols) is used.
   *  ^ toggles uppercase; # toggles the symbol layer. */
  const char* const* rows;
  uint8_t rowCount;

  uint8_t maxLen;

  const char* const* shuffleNames;
  uint8_t shuffleCount;

  int16_t buttonsY;

  const char* backLabel;
  const char* middleLabel;
  const char* okLabel;

  bool enableShuffle;
  bool requireNonEmpty;
  const char* emptyErrorMsg;

  /* Show dots instead of the characters. Has an initialiser because every
   * caller in the tree declares `OnScreenKeyboardConfig cfg;` and then
   * assigns each field by name -- a new field with no default would be
   * indeterminate in all of them, and this one decides whether a password
   * is drawn on the screen. */
  bool maskInput = false;
};

struct OnScreenKeyboardResult {
  String text;
  bool accepted;
  bool cancelled;
};

/** Use the shared standard keyboard; sets rows=nullptr and rowCount. */
void osKeyboardUseStandardLayout(OnScreenKeyboardConfig& cfg);

OnScreenKeyboardResult showOnScreenKeyboard(const OnScreenKeyboardConfig& cfg,
                                            const String& initial);

#endif
