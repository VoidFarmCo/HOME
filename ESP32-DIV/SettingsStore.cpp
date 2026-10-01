#include <ArduinoJson.h>
#include <SD.h>
#include "SettingsStore.h"
#include "utils.h"


/* Deserialising copies every key into the document, so the load side needs
 * more room than the save side for the same settings. Overflow does not
 * throw: deserializeJson returns NoMemory, settingsLoad returns false, and
 * the device boots on defaults with a card full of perfectly good settings
 * on it. Sized with headroom for that reason, and named once so the three
 * uses cannot drift apart. */
static constexpr size_t kSettingsJsonSize = 1024;

static AppSettings g_settings;
AppSettings& settings() { return g_settings; }

const LogAppEntry kLogApps[(int)LogApp::kCount] = {
  {"Surveillance",   &AppSettings::logSpotter},
  {"Jam Detector",   &AppSettings::logJamDet},
  {"Packet Monitor", &AppSettings::logPcap},
  {"ESB Sniffer",    &AppSettings::logEsb},
  {"Wardriver",      &AppSettings::logWardrive},
};

static SettingsLoadResult g_lastLoad = SettingsLoadResult::NotAttempted;

SettingsLoadResult settingsLastLoad() { return g_lastLoad; }

const char* settingsLastLoadText() {
  switch (g_lastLoad) {
    case SettingsLoadResult::Loaded:     return "loaded from SD";
    case SettingsLoadResult::NoFile:     return "none saved yet, using defaults";
    case SettingsLoadResult::Unreadable: return "unreadable, using defaults";
    case SettingsLoadResult::NoCard:     return "no SD card, using defaults";
    default:                             return "not attempted";
  }
}

bool sdLoggingAllowed(LogApp app) {
  if (!g_settings.logToSd) return false;
  const int i = (int)app;
  /* Not reachable from the enum, and cheap. A bad index here would read a
   * bool out of the middle of some other setting and call it permission. */
  if (i < 0 || i >= (int)LogApp::kCount) return false;
  return g_settings.*(kLogApps[i].field);
}

static const AccentOption kAccentPresets[] = {
  {"Orange", 0xFBE4},
  {"Green",  0xB721},
  {"Red",    0xF800},
  {"Cyan",   0x07FF},
  {"Purple", 0xF81F},
  {"Yellow", 0xFFE0},
  {"White",  0xFFFF},
};

uint8_t accentPresetClamp(uint8_t preset) {
  if (preset >= ACCENT_PRESET_COUNT) return 0;
  return preset;
}

uint16_t accentColor565(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].color565;
}

const char* accentPresetName(uint8_t preset) {
  return kAccentPresets[accentPresetClamp(preset)].name;
}

const char* settingsBoardProfileId() {
  return TOUCH_PROFILE_ID;
}

void settingsApplyBoardTouchDefaults() {
  auto& s = g_settings;
  s.touchXMin = TOUCH_X_MIN;
  s.touchXMax = TOUCH_X_MAX;
  s.touchYMin = TOUCH_Y_MIN;
  s.touchYMax = TOUCH_Y_MAX;
}

static bool settingsTouchSavedForBoard(const StaticJsonDocument<kSettingsJsonSize>& doc) {
  JsonObjectConst touch = doc["touch"];
  if (touch.isNull()) {
    return false;
  }
  if (!touch["xMin"].is<uint16_t>() || !touch["xMax"].is<uint16_t>() ||
      !touch["yMin"].is<uint16_t>() || !touch["yMax"].is<uint16_t>()) {
    return false;
  }
  const char* savedBoard = doc["board"] | "";
  if (savedBoard[0] == '\0') {
    return true;
  }
  return strcmp(savedBoard, TOUCH_PROFILE_ID) == 0;
}

static bool sd_mounted = false;
static bool mountSD() {

  if (sd_mounted) {
    if (SD.cardType() != CARD_NONE) return true;
    sd_mounted = false;
  }

  sd_mounted = isSDCardAvailable();
  return sd_mounted;
}

static bool ensureDir(const char* dirPath) {
  if (!mountSD()) return false;
  if (!SD.exists(dirPath)) {
    if (SD.mkdir(dirPath)) return true;

    if (dirPath && dirPath[0] == '/') {
      return SD.mkdir(dirPath + 1);
    }
    return false;
  }
  return true;
}

bool settingsLoad() {
  settingsApplyBoardTouchDefaults();
  sdRetryMount();
  if (!mountSD()) {
    g_lastLoad = SettingsLoadResult::NoCard;
    return false;
  }
  if (!SD.exists(SETTINGS_PATH)) {
    /* Not a failure. It is what a card looks like before anything has been
     * saved, and the caller has every right to carry on with defaults. */
    g_lastLoad = SettingsLoadResult::NoFile;
    return true;
  }

  File f = SD.open(SETTINGS_PATH, FILE_READ);
  if (!f) {
    g_lastLoad = SettingsLoadResult::Unreadable;
    return false;
  }

  StaticJsonDocument<kSettingsJsonSize> doc;
  DeserializationError err = deserializeJson(doc, f);
  f.close();
  if (err) {
    g_lastLoad = SettingsLoadResult::Unreadable;
    return false;
  }

  auto& s = g_settings;
  s.brightness      = doc["brightness"]      | s.brightness;
  s.theme           = (Theme)(uint8_t)(doc["theme"] | (uint8_t)s.theme);
  s.accentColor     = accentPresetClamp(doc["accentColor"] | s.accentColor);

  s.autoWifiScan    = doc["autoWifiScan"]    | s.autoWifiScan;
  s.autoBleScan     = doc["autoBleScan"]     | s.autoBleScan;
  s.stealthMode     = doc["stealthMode"]     | s.stealthMode;
  s.logToSd         = doc["logToSd"]         | s.logToSd;
  s.logJson         = doc["logJson"]         | s.logJson;
  s.logSpotter      = doc["logSpotter"]      | s.logSpotter;
  s.logJamDet       = doc["logJamDet"]       | s.logJamDet;
  s.logPcap         = doc["logPcap"]         | s.logPcap;
  s.logEsb          = doc["logEsb"]          | s.logEsb;
  s.logWardrive     = doc["logWardrive"]     | s.logWardrive;

  if (s.autoWifiScan != s.autoBleScan) {
    bool en = (s.autoWifiScan || s.autoBleScan);
    s.autoWifiScan = en;
    s.autoBleScan  = en;
  }

  if (settingsTouchSavedForBoard(doc)) {
    JsonObjectConst touch = doc["touch"];
    s.touchXMin = touch["xMin"] | s.touchXMin;
    s.touchXMax = touch["xMax"] | s.touchXMax;
    s.touchYMin = touch["yMin"] | s.touchYMin;
    s.touchYMax = touch["yMax"] | s.touchYMax;
  } else {
    settingsApplyBoardTouchDefaults();
  }

  g_lastLoad = SettingsLoadResult::Loaded;
  return true;
}

bool settingsSave() {
  sdRetryMount();

  if (!ensureDir("/config")) {
    sd_mounted = false;
    if (!ensureDir("/config")) return false;
  }

  File f = SD.open(SETTINGS_PATH, FILE_WRITE);
  if (!f) {
    sd_mounted = false;
    if (!mountSD()) return false;
    f = SD.open(SETTINGS_PATH, FILE_WRITE);
    if (!f) return false;
  }

  auto& s = g_settings;
  StaticJsonDocument<kSettingsJsonSize> doc;
  doc["board"]           = TOUCH_PROFILE_ID;
  doc["brightness"]      = s.brightness;
  doc["theme"]           = (uint8_t)s.theme;
  doc["accentColor"]     = s.accentColor;

  doc["autoWifiScan"]    = s.autoWifiScan;
  doc["autoBleScan"]     = s.autoBleScan;
  doc["stealthMode"]     = s.stealthMode;
  doc["logToSd"]         = s.logToSd;
  doc["logJson"]         = s.logJson;
  doc["logSpotter"]      = s.logSpotter;
  doc["logJamDet"]       = s.logJamDet;
  doc["logPcap"]         = s.logPcap;
  doc["logEsb"]          = s.logEsb;
  doc["logWardrive"]     = s.logWardrive;

  JsonObject t = doc.createNestedObject("touch");
  t["xMin"] = s.touchXMin;
  t["xMax"] = s.touchXMax;
  t["yMin"] = s.touchYMin;
  t["yMax"] = s.touchYMax;

  bool ok = serializeJson(doc, f) > 0;
  f.close();
  return ok;
}
