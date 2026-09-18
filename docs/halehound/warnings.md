# What `-w` was hiding

Upstream's patched `platform.txt` adds `-w` to `build.extra_flags.esp32`,
turning off every compiler warning. Turning it back on gives **184 warnings,
144 of them in the sketch**.

```bash
tools/build.sh warnings
```

## Getting warnings out of this build at all

Two things block it, and both look like the build ignoring you.

`--warnings all` on its own does nothing, because `build.extra_flags` is
appended *after* `compiler.warning_flags` in the compile recipe. Upstream's
`-w` therefore wins whatever level you ask for, which is presumably why it was
put in that variable rather than in `compiler.warning_flags`. You have to
override `build.extra_flags.esp32` to drop it.

The platform also bakes `-Werror=all` into both raised levels:

```
compiler.warning_flags.more=-Wall -Werror=all
compiler.warning_flags.all=-Wall -Werror=all -Wextra
```

So `--warnings all` fails the build on the first unused function and you never
see the other 180. `tools/build.sh setup` rewrites that line to plain
`-Wall -Wextra`.

## The distribution

| | count |
|---|---|
| sketch (`ESP32-DIV/*`) | 144 |
| bundled libraries | 35 |
| core / toolchain | 5 |

Sketch warnings by flag:

| flag | count |
|---|---|
| `-Wmissing-field-initializers` | 52 |
| macro redefinition (no flag) | 23 |
| `-Wunused-variable` | 23 |
| `-Wunused-function` | 14 |
| `-Wformat-truncation=` | 14 |
| `-Wunused-but-set-variable` | 10 |
| `-Warray-bounds` | 3 |
| `-Wdeprecated-declarations` | 2 |
| `-Wclass-memaccess` | 2 |
| `-Wtype-limits` | 1 |

By file: `wifi.cpp` 88, `bluetooth.cpp` 31, `subghz.cpp` 7, `gps.cpp` 7,
`rfid.cpp` 5, `utils.cpp` 5, `ducky.cpp` 1.

Most of it is noise. Three things are not.

## 1. Out-of-bounds write in the deauth frame builders (fixed)

```
wifi.cpp:3094: array subscript 26 is above array bounds of 'uint8_t [26]'
wifi.cpp:3102: array subscript 26 is above array bounds of 'uint8_t [26]'
wifi.cpp:4316: array subscript 26 is above array bounds of 'uint8_t [26]'
```

Both the captive portal and the deauther build their frame from a 26-byte
template and then do:

```c
cp_deauth_frame[26] = 7;
```

Valid indices are 0-25. The template is a well-formed 802.11 deauthentication
frame:

```
 0- 1  C0 00           frame control (management, deauth)
 2- 3  00 00           duration
 4- 9  FF FF FF FF FF FF   addr1, destination
10-15  CC CC ...       addr2, overwritten with the BSSID
16-21  CC CC ...       addr3, overwritten with the BSSID
22-23  00 00           sequence control
24-25  01 00           reason code, little-endian = 1
```

So the reason code lives at offsets **24-25**, not 26. The write was wrong in
three separate ways: it ran off the end of the array, it never reached the air
because the send only transmits 26 bytes, and the reason code it was trying to
set stayed at the template's value of 1 the whole time.

It was firing on every frame — twice per captive-portal send, once per
deauther send, both in hot loops — and getting away with it on alignment
padding:

```
deauth_frame      addr 0x3ffcec18  size 26  last valid byte 0x3ffcec31
                  frame[26] writes  0x3ffcec32
                  next symbol       0x3ffcec34  deautherLastButtonPress

cp_deauth_frame   addr 0x3ffcef98  size 26  last valid byte 0x3ffcefb1
                  frame[26] writes  0x3ffcefb2
                  next symbol       0x3ffcefb4  cp_last_deauth_time
```

Two bytes of slack in both cases. Resize an array, add a variable, reorder a
declaration or change compiler flags and the stray byte starts landing on a
live timing variable belonging to the same feature.

**Fixed** by removing the writes, which preserves what actually goes on the
air. If you want the reason code the author clearly intended — 7, class-3
frame from a nonassociated station — that is `cp_deauth_frame[24] = 7;`. That
is a real change to transmitted frames, so it is left as your call rather than
made silently.

## 2. Macros redefined to different values inside one file

Twenty-three redefinition warnings, and they are not duplicates of the same
value:

```
SCREEN_HEIGHT   320, 250, 180, 64      bluetooth.cpp, subghz.cpp, wifi.cpp
ICON_NUM        1, 2, 3, 5, 6          20 definitions across two files
MAX_LINES       15, 16, 23, 26, 30
MAX_SSID_LENGTH 8 then 10              wifi.cpp
STATUS_BAR_Y_OFFSET  0 in shared.h, then 20 in twenty places
```

Each redefinition silently changes meaning for every line below it. The sharp
edge is `MAX_LINES`, which is defined as an expression rather than a value:

```c
#define MAX_LINES (SCREEN_HEIGHT / LINE_HEIGHT)
String Buffer[MAX_LINES];
```

It is evaluated at each *use*, not where it is defined. `bluetooth.cpp:4087`
defines it while `SCREEN_HEIGHT` is 180, so `Buffer` gets 15 elements. At line
4321 `SCREEN_HEIGHT` becomes 320 again, and from there `MAX_LINES` evaluates
to 26 while `Buffer` still holds 15.

Checked: every `MAX_LINES` use in that block (4145, 4152, 4155, 4163) sits
above 4321, so **there is no live mismatch today**. It survives on the
ordering of the file. Moving a loop below a `#define` is all it would take.

Not fixed. Untangling it means giving these per-screen constants real names or
scopes, which is a refactor, not a warning cleanup.

## 3. Smaller real ones

**`-Wclass-memaccess` x2**, `bluetooth.cpp:5400` and `:7671`:
`memset(&s_devices[idx], 0, sizeof(MjDevice))` on a struct with default member
initializers. Formally UB, harmless here — both structs hold only scalars and
arrays, and all-zero matches their defaults. `s_devices[idx] = MjDevice{};`
would be correct.

**`-Wtype-limits`**, `utils.cpp:1971`: `applyBrightness(uint8_t v)` opens with
`if (v > 255) v = 255;`, which can never be true. The clamp is dead; anything
over 255 was already truncated at the call boundary.

**`-Wformat-truncation=` x14**: `snprintf` into buffers that a wide `%d` could
overflow, e.g. `"Page %d/%d"` into 20 bytes with `int` arguments. `snprintf`
truncates safely, so these are cosmetic unless a count genuinely gets large.

**`-Wdeprecated-declarations` x2**: `tcpip_adapter_init()` at `wifi.cpp:284`.
Works on IDF 4.4, gone in IDF 5. Relevant if the fork ever moves to core 3.x.

## Where this leaves things

`-w` is still in the build. The 181 remaining warnings are dominated by
`-Wmissing-field-initializers` and unused-symbol noise, so switching the
default build to `-Wall` today would just be a wall of text nobody reads.

The useful order is: clear the unused-symbol and missing-initializer noise,
then turn `-Wall -Wextra` on for real so the next `-Warray-bounds` shows up
the day it is written instead of being found by an archaeology session.
