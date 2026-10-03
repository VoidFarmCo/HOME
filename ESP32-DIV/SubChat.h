#pragma once
/* SubGHz > SubGHz Chat: a broadcast text chat over the CC1101 at 433.92 MHz.
 * Packet mode, a fixed H.O.M.E sync word so only our devices hear each other,
 * CRC on. No LoRa hardware needed -- it uses the sub-GHz radio already on the
 * hat. Peer to peer, no pairing: every message is heard by every nearby unit. */
namespace SubChat {
void setup();
void loop();
void exit();
}
