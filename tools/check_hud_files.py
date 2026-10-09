#!/usr/bin/env python3
"""SD file manager: list /sd, select, and delete (two-tap confirm), in settings.

Opened from a SETTINGS 'SD FILES' button; lists files with sizes, a tapped row
selects, DELETE needs a confirm tap (s_fmConfirm) before removing, CLOSE exits.
Backed by hud_sd_list + hud_sd_remove. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
SD = (H / "hud_sd.cpp").read_text(encoding="utf-8", errors="replace")
df = re.search(r"static void draw_files\(.*?\n\}", PG, re.S); DF = df.group(0) if df else ""
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("SD backend: list files (name+size) and remove one",
       "int hud_sd_list(char names[][24]" in SD and "bool hud_sd_remove(" in SD and "remove(full)" in SD)
    ok("a SETTINGS 'SD FILES' button opens the manager (refreshing the list)",
       "SET_FIL_Y" in PG and re.search(r"SET_FIL_Y.*s_files = true.*fm_refresh\(\)", PR, re.S) is not None)
    ok("the manager lists files with sizes, DELETE + CLOSE buttons",
       "s_fmName[idx]" in DF and "fm_size_str" in DF and '"DELETE"' in DF and '"CLOSE"' in DF)
    ok("DELETE needs a confirm tap before it removes the file",
       re.search(r"if \(!s_fmConfirm\) s_fmConfirm = true;\s*\n\s*else \{ hud_sd_remove\(s_fmName\[s_fmSel\]\); fm_refresh\(\)", PR) is not None)
    ok("the manager is modal in the input handler + guards a missing card",
       "if (s_files) {" in PR and "if (!hud_sd_ok()) { s_files = false; return; }" in PR)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
