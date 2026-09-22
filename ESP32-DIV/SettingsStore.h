#pragma once

#include <Arduino.h>
#include "shared.h"

#ifndef SETTINGS_PATH
#define SETTINGS_PATH "/config/settings.json"
#endif

struct AppSettings {

  uint8_t  brightness = BKL_LEVEL_MED;
  Theme    theme      = Theme::Dark;
  uint8_t  accentColor = 0;

  bool     autoWifiScan    = true;
  bool     autoBleScan     = true;

  /* Whether features may write their own log files.
   *
   * On by default, because that is what every release so far has done and a
   * setting that silently turns off a feature you already rely on is worse
   * than no setting. Off means the card is left alone: nothing appends,
   * nothing is created, and the features that would have logged say so on
   * their own screens rather than appearing to record into nothing.
   *
   * It does not cover files you ask for by name -- a saved .sub capture, a
   * DuckyScript, this settings file itself. Those are the point of pressing
   * the button, not a side effect of running the app. sdLoggingAllowed()
   * says which is which at each site. */
  bool     logToSd        = true;

  uint16_t touchXMin = TOUCH_X_MIN;
  uint16_t touchXMax = TOUCH_X_MAX;
  uint16_t touchYMin = TOUCH_Y_MIN;
  uint16_t touchYMax = TOUCH_Y_MAX;

};

struct AccentOption {
  const char* name;
  uint16_t color565;
};

constexpr uint8_t ACCENT_PRESET_COUNT = 7;

AppSettings& settings();

/* Ask before opening a log file. One accessor rather than settings().logToSd
 * spelled out at every site, so the sites are greppable and the check script
 * can find the ones that forgot. */
bool sdLoggingAllowed();

uint8_t accentPresetClamp(uint8_t preset);
uint16_t accentColor565(uint8_t preset);
const char* accentPresetName(uint8_t preset);

const char* settingsBoardProfileId();
void settingsApplyBoardTouchDefaults();
bool settingsLoad();
bool settingsSave();
