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

struct ChatMsg { char from[12]; char text[48]; bool me; };

void            hud_comms_begin();                 // init ESP-NOW (after WiFi STA is up)
void            hud_comms_enter();                 // entering COMMS: lock 2.4 GHz + channel
void            hud_comms_send(const char* text);  // broadcast an (encrypted) canned message
int             hud_comms_count();                 // messages in the log
const ChatMsg*  hud_comms_log();                   // newest-last ring buffer (<= COMMS_LOG)
const char*     hud_comms_name();                  // this unit's short name

extern const char* const HUD_QUICKMSG[];           // the canned quick-messages
int             hud_quickmsg_count();
