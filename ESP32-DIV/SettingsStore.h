#pragma once

#include <Arduino.h>
#include "shared.h"
#include "home_ui.h"   /* Profile enum + H.O.M.E UI identity, used by AppSettings */

/* shared.h owns this now, built from PUEO_DIR along with every other path
 * on the card. It was defined in both places behind its own #ifndef, so
 * whichever header came first won and the other was dead code that looked
 * authoritative. */
#include "shared.h"
#ifndef SETTINGS_PATH
#error "shared.h should have defined SETTINGS_PATH"
#endif

struct AppSettings {

  uint8_t  brightness = BKL_LEVEL_MED;
  Theme    theme      = Theme::Dark;
  uint8_t  accentColor = 4;   // default to the Purple preset (H.O.M.E brand), was 0 (Orange)

  /* Which audience profile is active (see home_ui.h). Curates the home screen,
   * wording and accent; every capability stays reachable under All Tools. */
  Profile  profile       = Profile::Home;
  /* False until the first-boot picker has run once. Drives whether the picker
   * shows, and persists so the picker asks exactly once per card. */
  bool     profileChosen = false;

  /* User's chat handle. Empty -> chat_core falls back to the auto H-XXXX MAC
   * name. 11 = 10 chars (chat_core CHAT_NAME_MAX) + NUL. */
  char     chatName[11] = "";

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

  /* Write log rows as JSON, one object per line, instead of CSV.
   *
   * Off by default. CSV is what every release so far has written and what
   * anything already parsing these files expects, so this is a thing you turn
   * on rather than a change that arrives under you.
   *
   * One object per line, not one document per file. A capture is append-only
   * and can run for hours: a document would mean holding it all in memory or
   * hand-writing the brackets, and a card pulled mid-write would leave a file
   * that parses as nothing at all rather than one short row.
   *
   * It does not reach the wardriver. That file is POSTed to WiGLE's upload
   * API, which takes their CSV and nothing else, so this leaves it alone
   * rather than producing something the upload would reject. The row on the
   * screen says so, because a format switch that one app quietly disobeys is
   * worse than no switch. */
  bool     logJson        = false;

  uint16_t touchXMin = TOUCH_X_MIN;
  uint16_t touchXMax = TOUCH_X_MAX;
  uint16_t touchYMin = TOUCH_Y_MIN;
  uint16_t touchYMax = TOUCH_Y_MAX;

};

struct AccentOption {
  const char* name;
  uint16_t color565;
};

constexpr uint8_t ACCENT_PRESET_COUNT = 9;

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
/* What the last settingsLoad() actually did.
 *
 * settingsLoad() returns bool, and returns true both when it read a file and
 * when it mounted a card that has no file on it yet. Those are the same
 * answer to "did anything go wrong" and different answers to "are my
 * settings in effect" -- and the boot line was reporting the first while
 * looking like it reported the second. A card with no settings.json printed
 * "settings loaded from SD".
 *
 * Kept as a separate query rather than a changed return type so every
 * existing caller keeps compiling and keeps meaning what it meant. */
enum class SettingsLoadResult : uint8_t {
  NotAttempted = 0,
  NoCard,        // nothing mounted; defaults, and nothing was written either
  NoFile,        // card mounted, no settings.json yet -- normal on a new card
  Unreadable,    // a file is there and could not be opened or parsed
  Loaded,        // read and applied
};

SettingsLoadResult settingsLastLoad();

/* One line for the serial log, naming which of the five it was. */
const char* settingsLastLoadText();

bool settingsLoad();
bool settingsSave();
