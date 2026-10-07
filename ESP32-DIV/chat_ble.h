#pragma once
/* BLE chat transport: short broadcast messages in the manufacturer-data field
 * of a BLE advertisement, over the ESP32's own radio -- no extra hardware, no
 * pairing. A legacy advertisement holds only ~24 bytes after its headers, so
 * unlike the other channels BLE carries SHORT messages (longer text is clipped
 * to what fits one advert). chat_core owns the name+text framing. */
#include <stdint.h>
namespace BleChat {
bool        available();                        // ESP32 BLE: always present
bool        init();                             // NimBLE scan (RX) + advertising (TX)
void        deinit();
bool        send(const uint8_t* data, uint8_t len);
uint8_t     poll(uint8_t* buf, uint8_t maxLen); // 0, or a received frame
const char* label();                            // "BLE"
}
