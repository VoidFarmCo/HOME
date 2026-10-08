#include "hud_comms.h"
#include "chat_crypt.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <string.h>
#include <stdio.h>

const char* const HUD_QUICKMSG[] = {
  "COPY", "HOLD", "MOVING", "CONTACT", "NEED BACKUP", "FALL BACK", "CLEAR", "SITREP"
};
int hud_quickmsg_count() { return (int)(sizeof(HUD_QUICKMSG) / sizeof(HUD_QUICKMSG[0])); }

static const uint8_t BCAST[6] = { 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF };
static ChatMsg  s_log[COMMS_LOG];
static int      s_count = 0;
static char     s_name[12] = "HUD";
static bool     s_ready = false;

static void push(const char* from, const char* text, bool me) {
  ChatMsg* m;
  if (s_count < COMMS_LOG) { m = &s_log[s_count++]; }
  else { memmove(&s_log[0], &s_log[1], sizeof(ChatMsg) * (COMMS_LOG - 1)); m = &s_log[COMMS_LOG - 1]; }
  strncpy(m->from, from, sizeof(m->from) - 1); m->from[sizeof(m->from) - 1] = 0;
  strncpy(m->text, text, sizeof(m->text) - 1); m->text[sizeof(m->text) - 1] = 0;
  m->me = me;
}

// H.O.M.E wire frame: [nameLen][name][text].
static int build_frame(const char* from, const char* text, uint8_t* f) {
  int nl = (int)strlen(from); if (nl > CHAT_NAME_MAX) nl = CHAT_NAME_MAX;
  int tl = (int)strlen(text); if (tl > CHAT_TEXT_MAX) tl = CHAT_TEXT_MAX;
  f[0] = (uint8_t)nl;
  memcpy(f + 1, from, nl);
  memcpy(f + 1 + nl, text, tl);
  return 1 + nl + tl;
}
static void parse_frame(const uint8_t* f, int len) {
  if (len < 1) return;
  int nl = f[0];
  if (nl > CHAT_NAME_MAX || 1 + nl > len) return;
  char from[CHAT_NAME_MAX + 1]; memcpy(from, f + 1, nl); from[nl] = 0;
  int tl = len - 1 - nl; if (tl < 0) tl = 0; if (tl > CHAT_TEXT_MAX) tl = CHAT_TEXT_MAX;
  char text[CHAT_TEXT_MAX + 1]; memcpy(text, f + 1 + nl, tl); text[tl] = 0;
  push(from, text, false);
}

// ESP-NOW receive (arduino-esp32 3.x). Decrypt, then parse the H.O.M.E frame.
static void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  uint8_t frame[CHAT_FRAME_MAX + 16];
  int fl = chat_decrypt(data, len, frame);
  if (fl > 0) parse_frame(frame, fl);
}

void hud_comms_begin() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(s_name, sizeof(s_name), "HUD%02X%02X", mac[4], mac[5]);  // <= 10 chars, unique

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;              // use the current channel (matches H.O.M.E chat_espnow)
  peer.encrypt = false;          // our privacy is the AES payload, not ESP-NOW's link crypto
  esp_now_add_peer(&peer);
  s_ready = true;
}

void hud_comms_enter() {
  WiFi.setBandMode(WIFI_BAND_MODE_2G_ONLY);
  esp_wifi_set_channel(COMMS_CH, WIFI_SECOND_CHAN_NONE);
}

void hud_comms_send(const char* text) {
  if (!s_ready) return;
  uint8_t frame[CHAT_FRAME_MAX];
  int fl = build_frame(s_name, text, frame);
  uint8_t enc[CHAT_FRAME_MAX + 16];
  int el = chat_encrypt(frame, fl, enc);
  if (el > 0) esp_now_send(BCAST, enc, el);
  push(s_name, text, true);        // echo our own into the log
}

int            hud_comms_count() { return s_count; }
const ChatMsg* hud_comms_log()   { return s_log; }
const char*    hud_comms_name()  { return s_name; }
