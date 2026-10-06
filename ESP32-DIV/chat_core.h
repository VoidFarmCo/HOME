#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * chat_core — one text-chat UI over interchangeable radios.
 *
 * The screen (name, scrolling log, on-screen keyboard) and the wire frame
 * ([nameLen][name...][text...]) live here and never change per radio. Each radio
 * is a TRANSPORT: a namespace that provides the same six calls
 *
 *     bool  available();                       // present/usable on this board now
 *     bool  init();                            // bring it up for a session
 *     void  deinit();                          // release it on exit
 *     bool  send(const uint8_t* d, uint8_t n); // ship one frame
 *     uint8_t poll(uint8_t* buf, uint8_t max); // 0, or a received frame's length
 *     const char* label();                     // channel name for the picker
 *
 * chat_core dispatches those by channel index in a switch (no function-pointer
 * table -- this board's DRAM is full). Add a radio = add a transport namespace +
 * one case in each switch; the UI is untouched. The Chat tile's group grid is the
 * channel picker: one tile per channel, each launching the core on that channel.
 * ──────────────────────────────────────────────────────────────────────────── */

#include <stdint.h>

namespace Chat {

/* The wire frame cap: name + text must leave room in the smallest transport FIFO
 * (CC1101's 64-byte packet). Shared by every channel so a message sent on one
 * fits when relayed/echoed on another. */
constexpr int CHAT_NAME_MAX = 10;
constexpr int CHAT_TEXT_MAX = 46;
constexpr int CHAT_FRAME_MAX = 1 + CHAT_NAME_MAX + CHAT_TEXT_MAX;   // 57

/* Channel registry (kept in step with the dispatch switches in chat_core.cpp). */
enum Channel : uint8_t { CH_SUBGHZ = 0, CH_ESPNOW = 1, CH_LORA = 2, CH_COUNT };

int         channelCount();            // == CH_COUNT
const char* channelLabel(int idx);     // transport label for the picker tile
bool        channelAvailable(int idx); // transport self-report (grey/notes if false)

/* Pick the channel BEFORE setup() (launchChatFeature does this). */
void selectChannel(int idx);

/* runSubmenuFeature-style lifecycle, driven on the selected channel. */
void setup();
void loop();
void exit();

}  // namespace Chat
