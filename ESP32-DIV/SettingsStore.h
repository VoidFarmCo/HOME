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

  /* Receive only: every tool whose job is to transmit refuses to start, and
   * the scans that transmit while looking like receivers are made passive.
   * Off by default -- this is a mode you choose, not a default that quietly
   * disables half the menu. See Stealth.h for what it does and does not
   * cover. */
  bool     stealthMode    = false;

  /* Whether features may write their own log files.
   *
   * logToSd is the master: off means the card is left alone by every one of
   * them. The five below are per feature, and a feature logs only when both
   * say yes. All on by default, because that is what every release so far
   * has done and a setting that silently turns off something you already
   * rely on is worse than no setting.
   *
   * None of this covers files you ask for by name -- a saved .sub capture, a
   * DuckyScript, this settings file itself. Those are the point of pressing
   * the button, not a side effect of running the app. sdLoggingAllowed()
   * says which is which at each site.
   *
   * Adding one here means adding it to LogApp and kLogApps below, and the
   * compiler will not tell you if you do not: check_settings.py does. */
  bool     logToSd        = true;
  bool     logSpotter     = true;
  bool     logJamDet      = true;
  bool     logPcap        = true;
  bool     logEsb         = true;
  bool     logWardrive    = true;

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

/* Which feature is asking. The order is the order the rows appear in
 * Settings > SD Logging, and kCount is what sizes that list -- so a feature
 * added here shows up on the screen without anybody editing the screen. */
enum class LogApp : uint8_t {
  Surveillance = 0,
  JamDetector,
  PacketMonitor,
  EsbSniffer,
  Wardriver,
  kCount
};

struct LogAppEntry {
  const char* label;          // as it reads on the settings row
  bool AppSettings::*field;
};

/* One table, read by both the accessor and the screen. Two tables is how the
 * screen ends up offering a switch that the accessor never consults. */
extern const LogAppEntry kLogApps[(int)LogApp::kCount];

/* Ask before opening a log file. Both switches have to agree, and a site
 * that names the wrong LogApp is a site that obeys somebody else's setting
 * -- which is why every call is checked by name in check_settings.py. */
bool sdLoggingAllowed(LogApp app);

uint8_t accentPresetClamp(uint8_t preset);
uint16_t accentColor565(uint8_t preset);
const char* accentPresetName(uint8_t preset);

const char* settingsBoardProfileId();
void settingsApplyBoardTouchDefaults();
bool settingsLoad();
bool settingsSave();
