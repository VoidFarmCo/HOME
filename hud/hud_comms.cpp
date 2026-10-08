#include "hud_comms.h"
#include "chat_crypt.h"
#include <WiFi.h>
#include <esp_now.h>
#include <esp_wifi.h>
#include <Preferences.h>
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

// active net
static char     s_netName[16] = "OPEN";
static uint8_t  s_netKey[16];
static uint16_t s_netId = 0;      // 0 = OPEN
static bool     s_netOpen = true;

static void push(const char* from, const char* text, bool me, bool open) {
  ChatMsg* m;
  if (s_count < COMMS_LOG) { m = &s_log[s_count++]; }
  else { memmove(&s_log[0], &s_log[1], sizeof(ChatMsg) * (COMMS_LOG - 1)); m = &s_log[COMMS_LOG - 1]; }
  strncpy(m->from, from, sizeof(m->from) - 1); m->from[sizeof(m->from) - 1] = 0;
  strncpy(m->text, text, sizeof(m->text) - 1); m->text[sizeof(m->text) - 1] = 0;
  m->me = me; m->open = open;
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
static void parse_frame(const uint8_t* f, int len, bool open) {
  if (len < 1) return;
  int nl = f[0];
  if (nl > CHAT_NAME_MAX || 1 + nl > len) return;
  char from[CHAT_NAME_MAX + 1]; memcpy(from, f + 1, nl); from[nl] = 0;
  int tl = len - 1 - nl; if (tl < 0) tl = 0; if (tl > CHAT_TEXT_MAX) tl = CHAT_TEXT_MAX;
  char text[CHAT_TEXT_MAX + 1]; memcpy(text, f + 1 + nl, tl); text[tl] = 0;
  push(from, text, false, open);
}

// ESP-NOW receive (arduino-esp32 3.x). Packet = [netId(2)][payload]. We accept the
// active net (decrypt) and always the OPEN net (plaintext); other nets are ignored.
static void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (len < 2) return;
  uint16_t id = (uint16_t)((data[0] << 8) | data[1]);
  const uint8_t* payload = data + 2; int plen = len - 2;
  uint8_t frame[CHAT_FRAME_MAX + 16];
  if (id == 0) {                                   // OPEN net: plaintext
    if (plen > 0 && plen <= CHAT_FRAME_MAX) { memcpy(frame, payload, plen); parse_frame(frame, plen, true); }
  } else if (!s_netOpen && id == s_netId) {        // our keyed net: decrypt
    int fl = chat_decrypt(s_netKey, payload, plen, frame);
    if (fl > 0) parse_frame(frame, fl, false);
  }
}

void hud_comms_set_net(const char* pass) {
  if (!pass || !*pass || !strcmp(pass, "OPEN")) {
    s_netOpen = true; s_netId = 0; strcpy(s_netName, "OPEN");
  } else {
    chat_key_from_pass(pass, s_netKey);
    s_netId = chat_net_id(s_netKey);
    s_netOpen = false;
    strncpy(s_netName, pass, sizeof(s_netName) - 1); s_netName[sizeof(s_netName) - 1] = 0;
  }
  Preferences p; p.begin("hudnet", false);
  p.putString("pass", s_netOpen ? "" : s_netName);
  p.end();
}

void hud_comms_begin() {
  uint8_t mac[6]; WiFi.macAddress(mac);
  snprintf(s_name, sizeof(s_name), "HUD%02X%02X", mac[4], mac[5]);  // <= 10 chars, unique

  Preferences p; p.begin("hudnet", true);
  char pass[16]; String s = p.getString("pass", ""); p.end();
  s.toCharArray(pass, sizeof(pass));
  hud_comms_set_net(pass);                         // "" -> OPEN

  if (esp_now_init() != ESP_OK) return;
  esp_now_register_recv_cb(on_recv);
  esp_now_peer_info_t peer = {};
  memcpy(peer.peer_addr, BCAST, 6);
  peer.channel = 0;              // use the current channel (matches H.O.M.E chat_espnow)
  peer.encrypt = false;          // privacy is the AES payload, not ESP-NOW link crypto
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
  uint8_t pkt[2 + CHAT_FRAME_MAX + 16];
  pkt[0] = (uint8_t)(s_netId >> 8); pkt[1] = (uint8_t)(s_netId & 0xFF);
  int plen;
  if (s_netOpen) { memcpy(pkt + 2, frame, fl); plen = fl; }
  else           { plen = chat_encrypt(s_netKey, frame, fl, pkt + 2); if (plen <= 0) return; }
  esp_now_send(BCAST, pkt, 2 + plen);
  push(s_name, text, true, s_netOpen);             // echo our own
}

int            hud_comms_count()    { return s_count; }
const ChatMsg* hud_comms_log()      { return s_log; }
const char*    hud_comms_name()     { return s_name; }
const char*    hud_comms_net()      { return s_netName; }
bool           hud_comms_net_open() { return s_netOpen; }
