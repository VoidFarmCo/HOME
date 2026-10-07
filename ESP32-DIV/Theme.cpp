#include "shared.h"
#include "SettingsStore.h"


UiPalette UI = { BG_Dark, FG_Dark, ICON_Dark, TEXT_Dark, 0x3166, LINE_Dark, L_Dark, 0xFBE4, GREEN };

uint16_t uiUniversalColor() {
  return UI.warn;
}

/* Named theme presets. Each sets the base colours (background, card fill, text,
 * lines, labels) together; the accent/icon/warn tint still follows the separate
 * accentColor setting, so theme and accent are two orthogonal knobs. Dark/Light
 * are the original two; the rest are the "more options" the owner asked for. */
struct ThemeColors { const char* name; uint16_t bg, fg, text, line, lable; };
static const ThemeColors kThemes[] = {
  {"Dark",     BG_Dark,  FG_Dark,  TEXT_Dark,  LINE_Dark,  L_Dark },
  {"Light",    BG_Light, FG_Light, TEXT_Light, LINE_Light, L_Light},
  {"Midnight", 0x0000,   0x1082,   0xFFFF,     0x4208,     0x2104 },  // true black
  {"Matrix",   0x0000,   0x0140,   0x07E0,     0x0320,     0x0220 },  // green on black
  {"Sand",     0xF7BA,   0xDEB1,   0x2965,     0xB5B6,     0xCE79 },  // warm light
  {"Contrast", 0x0000,   0x0000,   0xFFFF,     0xFFFF,     0x8410 },  // high contrast
};
static_assert(sizeof(kThemes) / sizeof(kThemes[0]) == THEME_PRESET_COUNT,
              "kThemes table and THEME_PRESET_COUNT disagree");

const char* themeName(uint8_t idx) {
  if (idx >= THEME_PRESET_COUNT) idx = 0;
  return kThemes[idx].name;
}

void applyThemeToPalette(Theme t) {
  const uint16_t universal = accentColor565(settings().accentColor);
  uint8_t i = (uint8_t)t;
  if (i >= THEME_PRESET_COUNT) i = 0;
  const ThemeColors& p = kThemes[i];
  UI.bg     = p.bg;
  UI.fg     = p.fg;
  UI.icon   = universal;
  UI.text   = p.text;
  UI.line   = p.line;
  UI.accent = UI_ACCENT;
  UI.lable  = p.lable;
  UI.warn   = universal;
  UI.ok     = GREEN;
}
