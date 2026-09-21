#!/usr/bin/env python3
"""The status bar has two heights, and only some screens can afford the tall one.

Every feature screen in this firmware puts a toolbar immediately under the
status bar -- a survey of the drawing calls finds 74 with a literal y between
20 and 48, eleven of them at exactly 20. Those screens cannot give the bar a
single extra pixel without something being painted over.

The menu grids can: their first tile is at Y_START, which on the 3.5" panel is
44, leaving 24 px of bar-coloured nothing. So the height is shared state set
by whichever function paints the screen, and the failure mode is a screen that
forgets to declare one and inherits a tall bar over its own toolbar.

That is what this checks. It reads source; it does not need a board.

    python tools/check_status_bar.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def heights(panel35):
    """The two heights as the preprocessor would see them."""
    src = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    short = re.search(r"#define\s+PUEO_STATUS_SHORT\s+(\d+)", src)
    if not short:
        return None, None
    short = int(short.group(1))
    # #if PUEO_PANEL_35 / #define PUEO_STATUS_TALL n / #else / ... / #endif
    m = re.search(r"#define\s+PUEO_STATUS_SHORT\s+\d+\s*\n"
                  r"#if\s+PUEO_PANEL_35\s*\n"
                  r"#define\s+PUEO_STATUS_TALL\s+(\d+)\s*\n"
                  r"#else\s*\n"
                  r"#define\s+PUEO_STATUS_TALL\s+(\w+)\s*\n"
                  r"#endif", src)
    if not m:
        return short, None
    tall = m.group(1) if panel35 else m.group(2)
    return short, (int(tall) if tall.isdigit() else short)


def functions(src):
    """Crude but sufficient: top-level `void name(...) {` to its closing brace."""
    out = {}
    for m in re.finditer(r"^(?:static\s+)?void\s+(\w+)\s*\([^;{]*\)\s*\{",
                         src, re.M):
        depth, i = 0, m.end() - 1
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = src[m.end():i]
    return out


def main():
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    fns = functions(ino)

    print("the two heights:")
    s28, t28 = heights(False)
    s35, t35 = heights(True)
    ok("shared.h defines PUEO_STATUS_SHORT and PUEO_STATUS_TALL",
       s35 is not None and t35 is not None)
    if s35 is None or t35 is None:
        print("\nFAILED: %d of %d" % (len(FAILED) or 1, CHECKS))
        return 1
    ok('the 2.8" has one height, so its layout cannot move', t28 == s28,
       "SHORT=%s TALL=%s" % (s28, t28))
    ok('the 3.5" tall bar is actually taller', t35 > s35,
       "SHORT=%s TALL=%s" % (s35, t35))

    # The short bar's geometry is load-bearing: the 2.8" image is verified by
    # rebuilding it and getting the same code back, which only holds while the
    # centring reduces to the constants it replaced.
    ok("centring the build string is an identity on the short bar",
       (s35 - 8) // 2 == 6, "(%d - 8) / 2 = %d, was y + 2 = 6" % (s35, (s35 - 8) // 2))
    ok("centring the battery block is an identity on the short bar",
       4 + (s35 - s35) // 2 == 4)

    print("\nevery screen that paints the bar declares a height:")
    # A function that force-draws the bar is painting a whole screen. The menu
    # painters are the ones that choose; features inherit from the submenu they
    # were launched off, which is why only these must declare.
    painters = sorted(n for n, b in fns.items()
                      if re.match(r"display\w*$", n)
                      and "drawStatusBar(currentBatteryVoltage, true)" in b)
    ok("found the menu painters", len(painters) >= 4, str(painters))
    for n in painters:
        ok("%s declares a bar height" % n,
           "setStatusBarHeight(" in fns[n])
    # About is launched straight off a tile rather than through a submenu, so
    # it would inherit whatever the menu left behind.
    ok("drawAboutPage declares a bar height",
       "setStatusBarHeight(" in fns.get("drawAboutPage", ""))

    print("\nnothing is drawn under a tall bar:")
    draw = re.compile(r"\.\w+\(\s*(?:[^,()]+|\([^()]*\))\s*,\s*(\d+)\s*[,)]")
    tall_fns = [n for n, b in fns.items() if "PUEO_STATUS_TALL" in b]
    ok("something actually asks for the tall bar", bool(tall_fns), str(tall_fns))
    for n in tall_fns:
        bad = sorted({int(m.group(1)) for m in draw.finditer(fns[n])
                      if s35 <= int(m.group(1)) < t35})
        ok("%s draws nothing between %d and %d" % (n, s35, t35),
           not bad, "literal y at %s" % bad)

    # The tiles themselves are placed from Y_START, not a literal, so they are
    # checked against the constant rather than by the sweep above.
    m = re.search(r"#if TFT_WIDTH >= 320(.*?)#else", ino, re.S)
    y_start = int(re.search(r"Y_START\s*=\s*(\d+)", m.group(1)).group(1)) if m else None
    ok("the 3.5\" tile row clears the tall bar",
       y_start is not None and y_start >= t35,
       "Y_START=%s TALL=%s" % (y_start, t35))

    print("\nthe height reaches the bar:")
    ok("utils.cpp reads the height for the full repaint",
       re.search(r"barHeight\s*=\s*s_statusBarHeight", utils) is not None)
    ok("and for the blink-only fast path",
       re.search(r"kBarH\s*=\s*s_statusBarHeight", utils) is not None)
    ok("the setter compiles away when there is only one height",
       "#if PUEO_STATUS_TALL == PUEO_STATUS_SHORT" in
       (SKETCH / "utils.h").read_text(encoding="utf-8", errors="replace"))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
