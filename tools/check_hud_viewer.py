#!/usr/bin/env python3
"""Log/file viewer: read a file on-device to decide before deleting it.

The file manager's VIEW button opens a modal showing the recent END of the file
(hud_sd_read_tail), split into scrollable lines; top/bottom taps scroll, middle
closes back to the manager. Reads source.
"""
import re, sys
from pathlib import Path
H = Path(__file__).resolve().parent.parent / "hud"
PG = (H / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
SD = (H / "hud_sd.cpp").read_text(encoding="utf-8", errors="replace")
vo = re.search(r"static void view_open\(.*?\n\}", PG, re.S); VO = vo.group(0) if vo else ""
dv = re.search(r"static void draw_view\(.*?\n\}", PG, re.S); DV = dv.group(0) if dv else ""
pr = re.search(r"void hud_on_press\(.*?\n\}", PG, re.S); PR = pr.group(0) if pr else ""
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("SD can read the recent end of a file (tail)",
       "int hud_sd_read_tail(" in SD and "SEEK_END" in SD and "fread(buf" in SD)
    ok("view_open reads the file and splits it into lines",
       "hud_sd_read_tail(name, s_vbuf" in VO and "s_vline[s_vlines++]" in VO)
    ok("draw_view renders the lines with a scroll position",
       "s_vline[li]" in DV and "s_vscroll + r" in DV)
    ok("the file manager VIEW button opens the viewer on the selected file",
       re.search(r"view_open\(s_fmName\[s_fmSel\]\); s_view = true;", PR) is not None)
    ok("viewer is modal: top/bottom scroll, middle closes",
       re.search(r"if \(s_view\).*s_vscroll -= 3.*s_vscroll \+= 3.*s_view = false", PR, re.S) is not None)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
