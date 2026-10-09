#pragma once
#include <stdint.h>

// Team chat over ESP-NOW, using H.O.M.E's chat wire frame so the Combat HUD and
// H.O.M.E devices are on ONE private network (frame encrypted with a shared key via
// chat_crypt.h). Frame = [nameLen][name...][text...], matching ESP32-DIV/chat_core.h.
// One radio -> the WiFi scanner is paused while on the COMMS page.
#define CHAT_NAME_MAX 10
#define CHAT_TEXT_MAX 46
#define CHAT_FRAME_MAX (1 + CHAT_NAME_MAX + CHAT_TEXT_MAX)   // 57 (same as H.O.M.E)
#define COMMS_CH   1          // locked 2.4 GHz channel both ends sit on
#define COMMS_LOG  16         // message ring-buffer depth

struct ChatMsg { char from[12]; char text[48]; bool me; bool open; };

void            hud_comms_begin();                 // init ESP-NOW (after WiFi STA is up)
void            hud_comms_enter();                 // entering COMMS: lock 2.4 GHz + channel
void            hud_comms_send(const char* text);  // broadcast on the active net
int             hud_comms_count();                 // messages in the ring (<= COMMS_LOG)
uint32_t        hud_comms_total();                 // monotonic total messages ever (for logging)
const ChatMsg*  hud_comms_log();                   // newest-last ring buffer (<= COMMS_LOG)
const char*     hud_comms_name();                  // this unit's short name
// Nets: a passphrase defines a private group ("" or "OPEN" = the open broadcast net).
void            hud_comms_set_net(const char* passphrase);
const char*     hud_comms_net();                   // active net name ("OPEN" or the passphrase)
bool            hud_comms_net_open();               // is the active net the open broadcast?

extern const char* const HUD_QUICKMSG[];           // the canned quick-messages
int             hud_quickmsg_count();

// ---- Blue-force tracking ----
// Teammates' GPS shared over the SAME private net as chat (keyed nets ONLY -- we never
// beacon your position on the OPEN net). A position beacon is tagged 0xFF, an invalid
// chat nameLen, so chat-only H.O.M.E devices drop it. Wire (before net crypto):
//   [0xFF][nameLen][name...][lat float32][lon float32]
// Friends age off the scope after FRIEND_AGE. One radio -> this only runs on COMMS.
#define FRIEND_MAX    8
#define FRIEND_AGE    120000     // ms a teammate stays plotted after last heard
#define POS_BEACON_MS 3000       // how often we beacon our own position (on COMMS)
struct FriendInfo { char name[12]; double lat, lon; uint32_t age_ms; };
void hud_comms_tick(uint32_t now, bool gps_valid, double lat, double lon);  // beacon; COMMS only
int  hud_comms_friend_count();                 // teammates heard within FRIEND_AGE
bool hud_comms_friend(int i, FriendInfo* out); // i-th fresh teammate (name + lat/lon + age)
bool hud_comms_beaconing();                    // are we broadcasting our position right now?

// Shared marks: a point you dropped and SENT, received by teammates on the keyed net.
void hud_comms_send_mark(double lat, double lon);  // broadcast a dropped point (keyed net only)
int  hud_comms_mark_count();                       // teammates' shared marks (within MARK_AGE)
bool hud_comms_mark(int i, FriendInfo* out);       // i-th shared mark (sender name + lat/lon + age)
