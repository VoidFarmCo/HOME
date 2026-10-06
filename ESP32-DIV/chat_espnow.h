#pragma once
/* ESP-NOW chat transport: connectionless broadcast over the ESP32's own Wi-Fi
 * radio -- no extra hardware, no AP, no pairing. Every H.O.M.E unit in range
 * (~100 m) hears each frame. Ships/receives the raw chat frame; chat_core owns
 * the name+text framing. Moderate range, moderate bandwidth. */
#include <stdint.h>
namespace EspNowChat {
bool        available();                        // ESP32 Wi-Fi: always present
bool        init();                             // STA mode + broadcast peer
void        deinit();
bool        send(const uint8_t* data, uint8_t len);
uint8_t     poll(uint8_t* buf, uint8_t maxLen); // 0, or a received frame
const char* label();                            // "ESP-NOW"
}
