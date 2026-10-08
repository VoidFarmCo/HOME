#pragma once
#include <stdint.h>
// AP vendor hint from a BSSID's OUI (first 3 bytes), looked up in a table taken
// straight from the IEEE OUI registry (see hud_oui.cpp, generated + verified).
// Returns a short vendor label, "RANDOMIZED" for a privacy MAC, or nullptr if
// the OUI isn't in the (partial, drone-first) table -- a miss is never a wrong hint.
const char* hud_oui_vendor(const uint8_t mac[6]);
