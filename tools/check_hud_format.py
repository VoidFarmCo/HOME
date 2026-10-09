#!/usr/bin/env python3
"""Deliberate SD FORMAT recovery (no auto-format at boot).

Boot never auto-formats (do_mount(false)); a FORMAT button (reachable from the file
manager even with no card) deliberately wipes+remounts to FAT32 behind a two-tap
confirm, after showing a wait message. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
SD = (H / "hud_sd.cpp").read_text(encoding="utf-8", errors="replace")
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("boot mount never auto-formats; format is a separate deliberate call",
       "do_mount(false)" in SD and "bool hud_sd_format()" in SD
       and ("esp_vfs_fat_sdcard_format" in SD or "do_mount(true)" in SD))
    ok("the file manager opens even with no card (to reach FORMAT)",
       "s_files = true; s_settings = false; fm_refresh();" in PR)
    ok("FORMAT needs a two-tap confirm and shows a wait message before blocking",
       "s_fmConfirm = true;" in PR and "FORMATTING SD..." in PG and "hud_sd_format();" in PR)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
