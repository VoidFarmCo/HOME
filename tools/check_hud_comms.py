#!/usr/bin/env python3
"""COMMS: ESP-NOW team chat with canned quick-messages.

Broadcasts short canned messages over ESP-NOW (no pairing); a recv callback logs
incoming ones. The COMMS page shows the log + a tappable quick-message grid, and a
tap broadcasts that message. One radio -> the loop PAUSES the WiFi scanner while on
the COMMS page and locks the channel. Reads source; no board.
"""
import re, sys
from pathlib import Path
SK = Path(__file__).resolve().parent.parent / "hud"
C   = (SK / "hud_comms.cpp").read_text(encoding="utf-8", errors="replace")
H   = (SK / "hud_comms.h").read_text(encoding="utf-8", errors="replace")
PG  = (SK / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
INO = (SK / "hud.ino").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("ESP-NOW init + broadcast peer + recv callback",
       "esp_now_init()" in C and "esp_now_register_recv_cb" in C
       and "esp_now_add_peer" in C and "0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF" in C)
    CRY = (SK / "chat_crypt.h").read_text(encoding="utf-8", errors="replace")
    ok("frames are PRIVATE: AES shared-key encrypt/decrypt (chat_crypt)",
       "HOME_CHAT_KEY" in CRY and "mbedtls_aes_crypt_ctr" in CRY
       and "chat_encrypt(" in C and "chat_decrypt(" in C)
    ok("uses H.O.M.E's [nameLen][name][text] frame (interoperable)",
       "CHAT_NAME_MAX 10" in H and "CHAT_TEXT_MAX 46" in H
       and re.search(r"f\[0\] = \(uint8_t\)nl", C) is not None)
    ok("send broadcasts a packet AND echoes it into the log",
       re.search(r"hud_comms_send\([^)]*\).*?esp_now_send\(BCAST.*?push\(s_name", C, re.S) is not None)
    ok("canned quick-messages exist (COPY/HOLD/MOVING/CONTACT...)",
       "HUD_QUICKMSG[]" in C and '"CONTACT"' in C and '"NEED BACKUP"' in C)
    ok("COMMS has a collapsible keyboard + a scrollable quick-message strip",
       "void page_comms(" in PG and "draw_kb(" in PG and "draw_strip(" in PG
       and "KB_ROWS" in PG and "HUD_QUICKMSG[idx]" in PG)
    ok("keyboard SEND broadcasts the typed text; a strip chip broadcasts a quick-message",
       "hud_comms_send(s_compose)" in PG
       and re.search(r"z >= 0\) hud_comms_send\(HUD_QUICKMSG", PG) is not None)
    ok("one radio: the loop PAUSES the scanner on COMMS + locks the channel",
       re.search(r"if \(m == M_COMMS\)\s*\{[^}]*hud_comms_enter\(\)", INO, re.S) is not None
       and re.search(r"else\s*\{\s*hud_scan_tick", INO) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
