#!/usr/bin/env python3
"""Remove the IR module. Pueo has no IR hardware.

ir.cpp is 3151 lines and pulls in IRremoteESP8266, whose protocol tables are
the bulk of it. Measured from the linked image before removal:

    IR sketch code        37026 bytes
    IRremoteESP8266       47931 bytes
                          ------------
                          84957 bytes

There is no IR LED and no IR receiver anywhere in the Pueo pin map, so none
of that was ever reachable on this board. With flash at 93% it is the
obvious thing to cut first.

The menu is the fiddly part. IR sits behind a nested "Other" layer, and the
Home layer maps submenu indices to layers positionally, so removing the entry
at index 0 shifts RFID and GPS down one. This handles both the index mapping
and the two dispatch regions (button path and touch path).

One-shot, line-anchored, refuses if anything does not match.
"""

import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"
INO = SKETCH / "ESP32-DIV.ino"


def cut_block(text, start_marker, end_marker, what):
    """Remove from start_marker up to (not including) end_marker."""
    i = text.find(start_marker)
    if i < 0:
        raise SystemExit("not found: %s" % what)
    j = text.find(end_marker, i + len(start_marker))
    if j < 0:
        raise SystemExit("no end for: %s" % what)
    return text[:i] + text[j:], j - i


def main():
    raw = INO.read_bytes()
    nl = "\r\n" if b"\r\n" in raw else "\n"
    s = raw.decode("utf-8", "surrogateescape").replace("\r\n", "\n")
    before = len(s)

    def sub(old, new, what, count=1):
        nonlocal s
        if s.count(old) != count:
            raise SystemExit("%s: expected %d match(es), found %d"
                             % (what, count, s.count(old)))
        s = s.replace(old, new)

    # 1. include
    sub('#include "ir.h"\n', "", "ir.h include")

    # 2. layer constants; RFID and GPS move down one
    sub("""static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_IR   = 1;
static constexpr uint8_t OTHER_LAYER_RFID = 2;
static constexpr uint8_t OTHER_LAYER_GPS  = 3;""",
        """static constexpr uint8_t OTHER_LAYER_HOME = 0;
static constexpr uint8_t OTHER_LAYER_RFID = 1;
static constexpr uint8_t OTHER_LAYER_GPS  = 2;""",
        "layer constants")

    # 3. Other menu loses its first entry
    sub("""const int other_NUM_SUBMENU_ITEMS = 4;""",
        """const int other_NUM_SUBMENU_ITEMS = 3;""", "other item count")
    sub("""const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "IR Remote",
    "RFID/NFC",""",
        """const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "RFID/NFC",""", "other menu labels")

    # 4. ir submenu tables
    sub("""const int ir_NUM_SUBMENU_ITEMS = 4;
const char *ir_submenu_items[ir_NUM_SUBMENU_ITEMS] = {
    "Record",
    "Saved Profile",
    "Universal Controller",
    "Back to Main Menu"};

""", "", "ir submenu items")

    # 5. updateActiveSubmenu branch
    sub("""            } else if (other_layer == OTHER_LAYER_IR) {
                active_submenu_items = ir_submenu_items;
                active_submenu_size = ir_NUM_SUBMENU_ITEMS;
                active_submenu_icons = ir_submenu_icons;
""", "            ", "updateActiveSubmenu branch")

    # 6. Home-layer index mapping, both regions. IR was index 0, so RFID and
    #    GPS shift down and the old index 2 branch goes.
    old_map = """            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_IR;
                other_menu_grid_initialized = false;
                last_other_menu_index = -1;
                current_submenu_index = 0;
                updateActiveSubmenu();
                submenu_initialized = false;
                displaySubmenu();
            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_RFID;"""
    new_map = """            } else if (current_submenu_index == 0) {
                other_layer = OTHER_LAYER_RFID;"""
    # Both the button path and the touch path carry an identical copy at the
    # same indentation, so each of these replaces two occurrences.
    sub(old_map, new_map, "home->IR/RFID mapping (both paths)", count=2)

    sub("""            } else if (current_submenu_index == 2) {
                other_layer = OTHER_LAYER_GPS;""",
        """            } else if (current_submenu_index == 1) {
                other_layer = OTHER_LAYER_GPS;""",
        "home->GPS mapping (both paths)", count=2)

    # 7. The two IR layer dispatch blocks, cut up to the RFID branch that
    #    follows each.
    # Both copies sit at the same indent, so cut until none remain rather
    # than assuming how many there are.
    start = "        } else if (other_layer == OTHER_LAYER_IR) {"
    end = "        } else if (other_layer == OTHER_LAYER_RFID) {"
    cuts = 0
    while start in s:
        s, n = cut_block(s, start, end, "IR dispatch block")
        cuts += 1
        print("  cut IR dispatch block: %d chars" % n)
    if cuts != 2:
        raise SystemExit("expected 2 IR dispatch blocks, cut %d" % cuts)

    # 8. icon table
    s = re.sub(r"const unsigned char \*ir_submenu_icons\[ir_NUM_SUBMENU_ITEMS\] = \{[^}]*\};\n\n?",
               "", s, count=1)
    if "ir_submenu_icons" in s:
        raise SystemExit("ir_submenu_icons still referenced")

    out = s.replace("\n", nl) if nl == "\r\n" else s
    INO.write_bytes(out.encode("utf-8", "surrogateescape"))
    print("  ESP32-DIV.ino: %d -> %d chars" % (before, len(s)))

    for f in ("ir.cpp", "ir.h"):
        p = SKETCH / f
        if p.exists():
            n = len(p.read_bytes())
            p.unlink()
            print("  removed %s (%d bytes)" % (f, n))

    leftover = [t for t in ("IRRemoteFeature", "IRSavedProfile",
                            "IRUniversalController", "ir_submenu",
                            "OTHER_LAYER_IR", 'ir.h')
                if t in s]
    if leftover:
        print("  ! still referenced: %s" % ", ".join(leftover), file=sys.stderr)
        return 1
    print("  no dangling IR references")
    return 0


if __name__ == "__main__":
    sys.exit(main())
