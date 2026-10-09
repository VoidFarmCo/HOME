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

static uint32_t s_total = 0;   // monotonic message count (for the logger; the ring itself scrolls)
static void push(const char* from, const char* text, bool me, bool open) {
  ChatMsg* m;
  if (s_count < COMMS_LOG) { m = &s_log[s_count++]; }
  else { memmove(&s_log[0], &s_log[1], sizeof(ChatMsg) * (COMMS_LOG - 1)); m = &s_log[COMMS_LOG - 1]; }
  strncpy(m->from, from, sizeof(m->from) - 1); m->from[sizeof(m->from) - 1] = 0;
  strncpy(m->text, text, sizeof(m->text) - 1); m->text[sizeof(m->text) - 1] = 0;
  m->me = me; m->open = open;
  s_total++;
}
uint32_t hud_comms_total() { return s_total; }

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

// ---- blue-force: teammate position store ----
#define POS_TYPE 0xFF
struct Friend { char name[12]; double lat, lon; uint32_t last; };
static Friend s_fr[FRIEND_MAX];
static int    s_frN = 0;
static bool   s_beaconing = false;

static void friend_update(const char* name, double lat, double lon) {
  uint32_t now = millis();
  for (int i = 0; i < s_frN; i++)                        // known teammate -> update
    if (!strcmp(s_fr[i].name, name)) { s_fr[i].lat = lat; s_fr[i].lon = lon; s_fr[i].last = now; return; }
  Friend* f;
  if (s_frN < FRIEND_MAX) f = &s_fr[s_frN++];
  else {                                                 // full: evict the oldest
    int o = 0; for (int i = 1; i < s_frN; i++) if (s_fr[i].last < s_fr[o].last) o = i; f = &s_fr[o];
  }
  strncpy(f->name, name, sizeof(f->name) - 1); f->name[sizeof(f->name) - 1] = 0;
  f->lat = lat; f->lon = lon; f->last = now;
}

// Position beacon frame: [0xFF][nameLen][name][lat f32][lon f32].
static void parse_pos_frame(const uint8_t* f, int len) {
  if (len < 2 || f[0] != POS_TYPE) return;
  int nl = f[1];
  if (nl < 1 || nl > CHAT_NAME_MAX || 2 + nl + 8 > len) return;
  char name[CHAT_NAME_MAX + 1]; memcpy(name, f + 2, nl); name[nl] = 0;
  if (!strcmp(name, s_name)) return;                     // ignore our own echo
  float lat, lon; memcpy(&lat, f + 2 + nl, 4); memcpy(&lon, f + 2 + nl + 4, 4);
  friend_update(name, (double)lat, (double)lon);
}

// ---- shared marks: a point a teammate dropped and SENT over the net ----
#define MARK_TYPE 0xFE
#define MARK_AGE  600000              // marks persist 10 min (longer than a position)
static Friend s_mk[FRIEND_MAX];       // same shape: sender name + lat/lon + last-heard
static int    s_mkN = 0;

static void mark_update(const char* name, double lat, double lon) {
  uint32_t now = millis();
  for (int i = 0; i < s_mkN; i++)                        // one latest mark per sender
    if (!strcmp(s_mk[i].name, name)) { s_mk[i].lat = lat; s_mk[i].lon = lon; s_mk[i].last = now; return; }
  Friend* m;
  if (s_mkN < FRIEND_MAX) m = &s_mk[s_mkN++];
  else { int o = 0; for (int i = 1; i < s_mkN; i++) if (s_mk[i].last < s_mk[o].last) o = i; m = &s_mk[o]; }
  strncpy(m->name, name, sizeof(m->name) - 1); m->name[sizeof(m->name) - 1] = 0;
  m->lat = lat; m->lon = lon; m->last = now;
}

// Mark frame: [0xFE][nameLen][name][lat f32][lon f32].
static void parse_mark_frame(const uint8_t* f, int len) {
  if (len < 2 || f[0] != MARK_TYPE) return;
  int nl = f[1];
  if (nl < 1 || nl > CHAT_NAME_MAX || 2 + nl + 8 > len) return;
  char name[CHAT_NAME_MAX + 1]; memcpy(name, f + 2, nl); name[nl] = 0;
  if (!strcmp(name, s_name)) return;
  float lat, lon; memcpy(&lat, f + 2 + nl, 4); memcpy(&lon, f + 2 + nl + 4, 4);
  mark_update(name, (double)lat, (double)lon);
}

// ESP-NOW receive (arduino-esp32 3.x). Packet = [netId(2)][payload]. We accept the
// active net (decrypt) and always the OPEN net (plaintext); other nets are ignored.
static void on_recv(const esp_now_recv_info_t* info, const uint8_t* data, int len) {
  (void)info;
  if (len < 2) return;
  uint16_t id = (uint16_t)((data[0] << 8) | data[1]);
  const uint8_t* payload = data + 2; int plen = len - 2;
  uint8_t frame[CHAT_FRAME_MAX + 16];
  if (id == 0) {                                   // OPEN net: plaintext (chat only)
    if (plen > 0 && plen <= CHAT_FRAME_MAX) { memcpy(frame, payload, plen); parse_frame(frame, plen, true); }
  } else if (!s_netOpen && id == s_netId) {        // our keyed net: decrypt
    int fl = chat_decrypt(s_netKey, payload, plen, frame);
    if (fl > 0) {
      if (frame[0] == POS_TYPE)       parse_pos_frame(frame, fl);   // blue-force position beacon
      else if (frame[0] == MARK_TYPE) parse_mark_frame(frame, fl);  // a teammate's shared mark
      else                            parse_frame(frame, fl, false);
    }
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

// ---- blue-force TX + tick + accessors ----
// Beacon our position on the ACTIVE KEYED net (never OPEN -- we don't announce our
// location in the clear). Frame [0xFF][nameLen][name][lat f32][lon f32], AES'd like chat.
static void send_pos(double lat, double lon) {
  if (!s_ready || s_netOpen) return;
  uint8_t frame[2 + CHAT_NAME_MAX + 8];
  int nl = (int)strlen(s_name); if (nl > CHAT_NAME_MAX) nl = CHAT_NAME_MAX;
  frame[0] = POS_TYPE; frame[1] = (uint8_t)nl;
  memcpy(frame + 2, s_name, nl);
  float la = (float)lat, lo = (float)lon;
  memcpy(frame + 2 + nl, &la, 4); memcpy(frame + 2 + nl + 4, &lo, 4);
  int fl = 2 + nl + 8;
  uint8_t pkt[2 + sizeof(frame) + 16];
  pkt[0] = (uint8_t)(s_netId >> 8); pkt[1] = (uint8_t)(s_netId & 0xFF);
  int plen = chat_encrypt(s_netKey, frame, fl, pkt + 2);
  if (plen <= 0) return;
  esp_now_send(BCAST, pkt, 2 + plen);
}

void hud_comms_tick(uint32_t now, bool gps_valid, double lat, double lon) {
  static uint32_t last = 0;
  s_beaconing = (s_ready && !s_netOpen && gps_valid);   // keyed net + a fix = we broadcast
  if (s_beaconing && now - last >= POS_BEACON_MS) { send_pos(lat, lon); last = now; }
}

int hud_comms_friend_count() {
  uint32_t now = millis(); int k = 0;
  for (int i = 0; i < s_frN; i++) if (now - s_fr[i].last < FRIEND_AGE) k++;
  return k;
}
bool hud_comms_friend(int i, FriendInfo* out) {
  uint32_t now = millis(); int k = 0;
  for (int j = 0; j < s_frN; j++) {
    if (now - s_fr[j].last >= FRIEND_AGE) continue;
    if (k == i) {
      strncpy(out->name, s_fr[j].name, sizeof(out->name) - 1); out->name[sizeof(out->name) - 1] = 0;
      out->lat = s_fr[j].lat; out->lon = s_fr[j].lon; out->age_ms = now - s_fr[j].last;
      return true;
    }
    k++;
  }
  return false;
}
bool hud_comms_beaconing() { return s_beaconing; }

// ---- shared mark TX + accessors ----
// Broadcast a dropped point on the active KEYED net (never OPEN). Same framing as a
// position beacon but tagged 0xFE; teammates store it and show it on their map.
void hud_comms_send_mark(double lat, double lon) {
  if (!s_ready || s_netOpen) return;
  uint8_t frame[2 + CHAT_NAME_MAX + 8];
  int nl = (int)strlen(s_name); if (nl > CHAT_NAME_MAX) nl = CHAT_NAME_MAX;
  frame[0] = MARK_TYPE; frame[1] = (uint8_t)nl;
  memcpy(frame + 2, s_name, nl);
  float la = (float)lat, lo = (float)lon;
  memcpy(frame + 2 + nl, &la, 4); memcpy(frame + 2 + nl + 4, &lo, 4);
  int fl = 2 + nl + 8;
  uint8_t pkt[2 + sizeof(frame) + 16];
  pkt[0] = (uint8_t)(s_netId >> 8); pkt[1] = (uint8_t)(s_netId & 0xFF);
  int plen = chat_encrypt(s_netKey, frame, fl, pkt + 2);
  if (plen <= 0) return;
  esp_now_send(BCAST, pkt, 2 + plen);
  push(s_name, "[MARK SENT]", true, false);         // confirm it in the chat log
}

int hud_comms_mark_count() {
  uint32_t now = millis(); int k = 0;
  for (int i = 0; i < s_mkN; i++) if (now - s_mk[i].last < MARK_AGE) k++;
  return k;
}
bool hud_comms_mark(int i, FriendInfo* out) {
  uint32_t now = millis(); int k = 0;
  for (int j = 0; j < s_mkN; j++) {
    if (now - s_mk[j].last >= MARK_AGE) continue;
    if (k == i) {
      strncpy(out->name, s_mk[j].name, sizeof(out->name) - 1); out->name[sizeof(out->name) - 1] = 0;
      out->lat = s_mk[j].lat; out->lon = s_mk[j].lon; out->age_ms = now - s_mk[j].last;
      return true;
    }
    k++;
  }
  return false;
}
