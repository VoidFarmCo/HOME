#pragma once
#include <stdint.h>

// ENGAGE (threat detect): passive WiFi attack detector. In promiscuous mode, hopping
// the 2.4 GHz channels, it counts 802.11 deauth/disassoc frames -- the signature of a
// deauth/jam attack nearby. RX only (no transmission). One radio, so the WiFi scanner
// is paused while on the ENGAGE page (the loop handles that, like COMMS).
void     hud_engage_enter();          // promiscuous on, start hopping
void     hud_engage_leave();          // promiscuous off
void     hud_engage_tick(uint32_t now);
uint32_t hud_engage_frames();         // total mgmt frames seen (sniffer alive if climbing)
uint32_t hud_engage_deauth();         // deauth + disassoc frames seen
uint32_t hud_engage_last_ms();        // millis() of the last deauth (0 = none)
int      hud_engage_channel();        // channel currently being sniffed
