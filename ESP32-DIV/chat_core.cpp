#include "chat_core.h"
#include "shared.h"
#include "utils.h"
#include "KeyboardUI.h"
#include "Stealth.h"
#include "SettingsStore.h"
#include "chat_subghz.h"
#include "chat_espnow.h"
#include "chat_lora.h"

namespace Chat {

/* ── channel dispatch ─────────────────────────────────────────────────────────
 * One case per channel; adding a radio touches only these five switches and the
 * enum in chat_core.h. No table of function pointers (DRAM is full). */

int channelCount() { return CH_COUNT; }

const char* channelLabel(int idx) {
  switch (idx) {
    case CH_SUBGHZ: return SubghzChat::label();
    case CH_ESPNOW: return EspNowChat::label();
    case CH_LORA:   return LoRaChat::label();
    default:        return "?";
  }
}

bool channelAvailable(int idx) {
  switch (idx) {
    case CH_SUBGHZ: return SubghzChat::available();
    case CH_ESPNOW: return EspNowChat::available();
    case CH_LORA:   return LoRaChat::available();
    default:        return false;
  }
}

static int s_ch = CH_SUBGHZ;
void selectChannel(int idx) { s_ch = (idx >= 0 && idx < CH_COUNT) ? idx : CH_SUBGHZ; }

static bool txInit() {
  switch (s_ch) {
    case CH_SUBGHZ: return SubghzChat::init();
    case CH_ESPNOW: return EspNowChat::init();
    case CH_LORA:   return LoRaChat::init();
    default:        return false;
  }
}
static void txDeinit() {
  switch (s_ch) {
    case CH_SUBGHZ: SubghzChat::deinit(); break;
    case CH_ESPNOW: EspNowChat::deinit(); break;
    case CH_LORA:   LoRaChat::deinit();   break;
    default: break;
  }
}
static bool txSend(const uint8_t* d, uint8_t n) {
  switch (s_ch) {
    case CH_SUBGHZ: return SubghzChat::send(d, n);
    case CH_ESPNOW: return EspNowChat::send(d, n);
    case CH_LORA:   return LoRaChat::send(d, n);
    default:        return false;
  }
}
static uint8_t txPoll(uint8_t* buf, uint8_t maxLen) {
  switch (s_ch) {
    case CH_SUBGHZ: return SubghzChat::poll(buf, maxLen);
    case CH_ESPNOW: return EspNowChat::poll(buf, maxLen);
    case CH_LORA:   return LoRaChat::poll(buf, maxLen);
    default:        return 0;
  }
}

/* ── session state (heap: this board's .bss is full) ──────────────────────────*/

static constexpr int CHAT_LOG_LINES = 16;

struct ChatState {
  char log[CHAT_LOG_LINES][52];
  char name[CHAT_NAME_MAX + 1];
  int  logCount;
  bool radioUp;
};
static ChatState* s_cs = nullptr;

static void deriveName() {
  const char* custom = settings().chatName;
  if (custom && custom[0]) {                       // user-set name wins
    snprintf(s_cs->name, sizeof(s_cs->name), "%s", custom);
    return;
  }
  const uint64_t id = ESP.getEfuseMac();            // else the auto MAC handle
  snprintf(s_cs->name, sizeof(s_cs->name), "H-%04X", (unsigned)(uint16_t)id);
}

static void logLine(const char* who, const char* text) {
  if (!s_cs) return;
  char line[52];
  snprintf(line, sizeof(line), "%s: %s", who, text);
  if (s_cs->logCount < CHAT_LOG_LINES) {
    strncpy(s_cs->log[s_cs->logCount], line, 51);
    s_cs->log[s_cs->logCount][51] = 0;
    s_cs->logCount++;
  } else {
    for (int i = 1; i < CHAT_LOG_LINES; i++) memcpy(s_cs->log[i - 1], s_cs->log[i], 52);
    strncpy(s_cs->log[CHAT_LOG_LINES - 1], line, 51);
    s_cs->log[CHAT_LOG_LINES - 1][51] = 0;
  }
}

static void draw() {
  if (!s_cs) return;
  tft.setTextFont(1);
  tft.setTextSize(1);
  char hdr[44];
  snprintf(hdr, sizeof(hdr), "%s chat  %s", channelLabel(s_ch), s_cs->name);
  tft.fillRect(0, 40, PUEO_SCREEN_W, 14, TFT_BLACK);
  tft.setCursor(8, 42);
  tft.setTextColor(UI_ACCENT, TFT_BLACK);
  tft.print(hdr);

  const int top = 58, rowH = 13;
  const int bottom = featureHasTouchNavBar() ? (int)touchNavContentBottomY() : 300;
  const int maxRows = (bottom - top) / rowH;
  const int first = (s_cs->logCount > maxRows) ? (s_cs->logCount - maxRows) : 0;
  for (int i = 0; i < maxRows; i++) {
    const int y = top + i * rowH;
    tft.fillRect(0, y, PUEO_SCREEN_W, rowH, TFT_BLACK);
    const int idx = first + i;
    if (idx < s_cs->logCount) {
      tft.setCursor(6, y);
      const bool mine = (strncmp(s_cs->log[idx], s_cs->name, strlen(s_cs->name)) == 0);
      tft.setTextColor(mine ? UI_ACCENT : 0xFFFF, TFT_BLACK);
      tft.print(s_cs->log[idx]);
    }
  }
}

/* Build [nameLen][name][text] and hand it to the active transport. */
static void sendMessage(const char* text) {
  if (!s_cs || !s_cs->radioUp) return;
  uint8_t buf[CHAT_FRAME_MAX];
  int nameLen = (int)strlen(s_cs->name);
  if (nameLen > CHAT_NAME_MAX) nameLen = CHAT_NAME_MAX;
  int textLen = (int)strlen(text);
  if (textLen > CHAT_TEXT_MAX) textLen = CHAT_TEXT_MAX;
  int p = 0;
  buf[p++] = (uint8_t)nameLen;
  memcpy(buf + p, s_cs->name, nameLen); p += nameLen;
  memcpy(buf + p, text, textLen);       p += textLen;
  if (txSend(buf, (uint8_t)p)) {
    logLine(s_cs->name, text);
    draw();
  }
}

/* Pull one frame from the transport and parse [nameLen][name][text]. */
static void pollReceive() {
  if (!s_cs || !s_cs->radioUp) return;
  uint8_t buf[CHAT_FRAME_MAX];
  const uint8_t len = txPoll(buf, (uint8_t)sizeof(buf));
  if (len < 1) return;
  int nameLen = buf[0];
  if (nameLen < 1 || nameLen > CHAT_NAME_MAX || nameLen + 1 > len) return;
  char who[CHAT_NAME_MAX + 1];
  memcpy(who, buf + 1, nameLen);
  who[nameLen] = 0;
  char text[CHAT_TEXT_MAX + 1];
  int textLen = len - 1 - nameLen;
  if (textLen < 0) textLen = 0;
  if (textLen > CHAT_TEXT_MAX) textLen = CHAT_TEXT_MAX;
  memcpy(text, buf + 1 + nameLen, textLen);
  text[textLen] = 0;
  logLine(who, text);
  draw();
}

static void compose() {
  // OnScreenKeyboardConfig has no default initializers: every field the keyboard
  // reads must be set here or it is an indeterminate stack value (a garbage
  // pointer crashes TFT_eSPI::textWidth). Set the full set -- no shuffle.
  OnScreenKeyboardConfig cfg;
  osKeyboardUseStandardLayout(cfg);
  cfg.titleLine1 = channelLabel(s_ch);
  cfg.titleLine2 = "Message";
  cfg.maxLen = CHAT_TEXT_MAX;
  cfg.buttonsY = 195;
  cfg.backLabel = "Cancel";
  cfg.middleLabel = nullptr;
  cfg.okLabel = "Send";
  cfg.enableShuffle = false;
  cfg.shuffleNames = nullptr;
  cfg.shuffleCount = 0;
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg = "Type a message first";
  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setTouchNavLabels("Type", nullptr, "Exit", nullptr, nullptr);
  redrawTouchButtonBar();
  draw();
  if (r.accepted && r.text.length() > 0) {
    sendMessage(r.text.c_str());
  }
}

void setup() {
  if (Stealth::refuse("Chat")) return;
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  featureClearContent(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setTouchNavLabels("Type", nullptr, "Exit", nullptr, nullptr);
  redrawTouchButtonBar();
  setupTouchscreen();

  if (!s_cs) s_cs = new ChatState();
  if (!s_cs) return;
  s_cs->logCount = 0;
  s_cs->radioUp = false;
  deriveName();
  draw();
  if (channelAvailable(s_ch) && txInit()) {
    s_cs->radioUp = true;
    char msg[32];
    snprintf(msg, sizeof(msg), "on %s", channelLabel(s_ch));
    logLine("sys", msg);
  } else {
    char msg[32];
    snprintf(msg, sizeof(msg), "no %s radio", channelLabel(s_ch));
    logLine("sys", msg);
  }
  draw();
}

void loop() {
  if (featureExitButtonPressed() || isButtonPressed(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isButtonPressed(BTN_LEFT)) {   // "Type"
    delay(200);
    compose();
    return;
  }
  pollReceive();
  delay(10);
}

void exit() {
  if (s_cs && s_cs->radioUp) txDeinit();
  if (s_cs) { delete s_cs; s_cs = nullptr; }
}

}  // namespace Chat
