#pragma once
/* LoRa chat transport: the private H.O.M.E mesh (mesh.h) over the Core1262 /
 * SX1262 (Sx1262.h). send() wraps the chat frame as a fresh mesh packet; poll()
 * unwraps a received one, re-broadcasts it if it still has hops, and returns the
 * decrypted frame only when it is new. Longest range, true multi-hop. */
#include <stdint.h>
namespace LoRaChat {
bool        available();                        // SX1262 answers on the bus
bool        init();                             // bring the radio up + start RX
void        deinit();
bool        send(const uint8_t* data, uint8_t len);
uint8_t     poll(uint8_t* buf, uint8_t maxLen); // 0, or a new decrypted frame
const char* label();                            // "LoRa Mesh"
}
