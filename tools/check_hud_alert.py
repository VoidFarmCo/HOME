#!/usr/bin/env python3
"""Global threat alert: a cross-page warning on the top strip.

When the ENGAGE sniffer holds a live drone (drone_count>0) or a recent deauth
(last_ms within a few seconds), draw_chrome -- which runs on EVERY page -- strobes
the top status strip red with a threat label, so the warning follows you off
RADAR/ENGAGE onto any other page for the contact's live window. Reads source.
"""
import re, sys
from pathlib import Path
PG = (Path(__file__).resolve().parent.parent / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    # the alert lives inside draw_chrome (the shared chrome drawn on every page)
    m = re.search(r"static void draw_chrome\([^)]*\)\s*\{(.*?)\n\}", PG, re.S)
    body = m.group(1) if m else ""
    ok("alert is inside draw_chrome (runs on every page)",
       "hud_engage_drone_count()" in body and "hud_engage_last_ms()" in body)
    ok("fires on a live drone OR a recent deauth",
       "> 0" in body and re.search(r"millis\(\)\s*-\s*\w+\s*<\s*\d+", body) is not None)
    ok("strobes the top strip red with a threat label",
       "HUD_C_RED" in body and "STAT_H" in body
       and ("DRONE" in body and "ATTACK" in body))
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
