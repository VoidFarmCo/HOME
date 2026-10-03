#include "shared.h"
#include "utils.h"
#include "KeyboardUI.h"
#include "Branding.h"
#include "Stealth.h"
#include <ELECHOUSE_CC1101_SRC_DRV.h>

/* Bounded CC1101 presence check (subghz.cpp). ELECHOUSE's Init() below opens
 * with an unbounded `while (digitalRead(MISO));` that hangs forever with no
 * module wired, so radioInit() is only ever called once this says a chip is
 * there -- otherwise the chat still opens and exits, it just cannot transmit. */
bool subghzCc1101Present();

namespace SubChat {

/* Payload on the air: [nameLen][name...][text...]. Capped so the whole packet
 * fits the CC1101's 64-byte FIFO with room to spare. */
static constexpr int CHAT_NAME_MAX = 10;
static constexpr int CHAT_TEXT_MAX = 46;
static constexpr int CHAT_PKT_MAX  = 1 + CHAT_NAME_MAX + CHAT_TEXT_MAX;   // 57
static constexpr int CHAT_LOG_LINES = 16;
static constexpr float CHAT_MHZ = 433.92f;

/* All chat state is heap-allocated while the feature runs; this board's DRAM is
 * full, so only the pointer lives in .bss. */
struct ChatState {
  char log[CHAT_LOG_LINES][52];
  char name[CHAT_NAME_MAX + 1];
  int  logCount;
  bool radioUp;
};
static ChatState* s_cs = nullptr;

static void deriveName() {
  const uint64_t id = ESP.getEfuseMac();
  snprintf(s_cs->name, sizeof(s_cs->name), "H-%04X", (unsigned)(uint16_t)id);
}

static void radioInit() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(1);          // packet mode (FIFO + packet handler)
  ELECHOUSE_cc1101.setModulation(0);      // 2-FSK
  ELECHOUSE_cc1101.setMHZ(CHAT_MHZ);
  ELECHOUSE_cc1101.setDRate(4.8);         // 4.8 kbps -- robust, plenty for text
  ELECHOUSE_cc1101.setSyncMode(2);        // 16/16 sync bits
  ELECHOUSE_cc1101.setSyncWord(0x48, 0x4D);  // "HM": the H.O.M.E net
  ELECHOUSE_cc1101.setCrc(1);             // drop corrupt packets in hardware
  ELECHOUSE_cc1101.setLengthConfig(1);    // variable length (length byte first)
  ELECHOUSE_cc1101.setPacketLength((byte)CHAT_PKT_MAX);
  ELECHOUSE_cc1101.SetRx();
  s_cs->radioUp = true;
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
  char hdr[40];
  snprintf(hdr, sizeof(hdr), "SubGHz Chat 433.92  %s", s_cs->name);
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

static void sendMessage(const char* text) {
  if (!s_cs || !s_cs->radioUp) return;
  uint8_t buf[CHAT_PKT_MAX];
  int nameLen = (int)strlen(s_cs->name);
  if (nameLen > CHAT_NAME_MAX) nameLen = CHAT_NAME_MAX;
  int textLen = (int)strlen(text);
  if (textLen > CHAT_TEXT_MAX) textLen = CHAT_TEXT_MAX;
  int p = 0;
  buf[p++] = (uint8_t)nameLen;
  memcpy(buf + p, s_cs->name, nameLen); p += nameLen;
  memcpy(buf + p, text, textLen);       p += textLen;
  ELECHOUSE_cc1101.SendData(buf, (byte)p);
  ELECHOUSE_cc1101.SetRx();              // back to listening after the burst
  logLine(s_cs->name, text);
  draw();
}

static void pollReceive() {
  if (!s_cs || !s_cs->radioUp) return;
  if (!ELECHOUSE_cc1101.CheckReceiveFlag()) return;
  uint8_t buf[64];
  const int len = (int)ELECHOUSE_cc1101.ReceiveData(buf);
  ELECHOUSE_cc1101.SetRx();
  if (len < 1 || !ELECHOUSE_cc1101.CheckCRC()) return;
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
  OnScreenKeyboardConfig cfg;
  osKeyboardUseStandardLayout(cfg);
  cfg.titleLine1 = "SubGHz Chat";
  cfg.titleLine2 = "Message";
  cfg.maxLen = CHAT_TEXT_MAX;
  cfg.okLabel = "Send";
  cfg.backLabel = "Cancel";
  cfg.requireNonEmpty = true;
  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");
  // The keyboard repainted the screen; rebuild ours either way.
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
  if (Stealth::refuse("SubGHz Chat")) return;
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
  if (subghzCc1101Present()) {       // only Init a chip that is actually there
    radioInit();
    logLine("sys", "listening on 433.92");
  } else {
    logLine("sys", "no CC1101 found");
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
  if (s_cs && s_cs->radioUp) {
    ELECHOUSE_cc1101.setSidle();
  }
  if (s_cs) { delete s_cs; s_cs = nullptr; }
}

}  // namespace SubChat
