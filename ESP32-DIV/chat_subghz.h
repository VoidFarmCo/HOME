#pragma once
/* SubGHz chat transport: broadcast over the CC1101 at 433.92 MHz, packet mode,
 * the H.O.M.E sync word so only our units hear it, hardware CRC. Ships/receives
 * the raw chat frame; chat_core owns the name+text framing. No LoRa hardware. */
#include <stdint.h>
namespace SubghzChat {
bool        available();                        // CC1101 present (bounded probe)
bool        init();                             // bring the radio up for a session
void        deinit();                           // idle the radio
bool        send(const uint8_t* data, uint8_t len);
uint8_t     poll(uint8_t* buf, uint8_t maxLen); // 0, or received frame length
const char* label();                            // "SubGHz"
}
