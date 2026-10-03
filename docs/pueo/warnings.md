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

It was firing on every frame (twice per captive-portal send, once per
deauther send, both in hot loops), and getting away with it on alignment
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
air. If you want the reason code the author clearly intended (7, class-3
frame from a nonassociated station) that is `cp_deauth_frame[24] = 7;`. That
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
initializers. Formally UB, harmless here, both structs hold only scalars and
arrays, and all-zero matches their defaults. `s_devices[idx] = MjDevice{};`
would be correct.

**`-Wtype-limits`**, `utils.cpp:1971`: `applyBrightness(uint8_t v)` opens with
`if (v > 255) v = 255;`, which can never be true. The clamp is dead; anything
over 255 was already truncated at the call boundary.

**`-Wformat-truncation=` x14** (fixed): `snprintf` into buffers a wide `%d`
could overflow. Twelve were buffer sizing, six identical `char page_buf[20]`
holding `"Page %d/%d"`, which needs 29 bytes worst case; `char buf[48]` for
`"[!] cred %s / %s"` over two `char[32]`, which needs 75. Widened to the worst
case the format can produce. `snprintf` was truncating safely, so nothing
changes except in cases that were being silently cut.

The other two were not sizing problems, and widening would have been the wrong
fix. Both parse NMEA straight off the GPS serial line and neither checks it
got a number, so the values reaching `%02d` are genuinely unbounded:
`formatUtcFromField` does digit arithmetic on arbitrary bytes, and
`wardDdMmYyToIso` formats whatever `sscanf("%d/%d/%d")` returns. Both now
validate, which bounds the output, silences the warning, and stops a
malformed sentence writing a nonsense timestamp or a garbage date into the
wigle export.

One wrinkle worth recording: in `formatUtcFromField` the digit loop alone did
not satisfy gcc 8, which does not carry the loop's range information through
to the `field[i]` reads at the `snprintf`. An explicit `h`/`m`/`s` range check
immediately before the call does, and rejecting impossible times like
`99:99:99` is worth having regardless.

**`-Wdeprecated-declarations` x2**: `tcpip_adapter_init()` at `wifi.cpp:284`.
Works on IDF 4.4, gone in IDF 5. Relevant if the fork ever moves to core 3.x.

## Cleanup pass

`tools/silence_unused.py` cleared all 47 `-Wunused-*` warnings in the sketch,
and the two `memset` calls became value-initialization. Sketch warnings went
144 -> 92; under `-Wall` alone, ~92 -> 39.

Functions and variables got different treatment on purpose.

The 14 unused **functions** are marked `__attribute__((unused))`, not deleted.
They cost nothing to keep: the platform builds with `-ffunction-sections
-fdata-sections` and links with `--gc-sections`, and none of them appear in
the linked image. Deleting them would buy zero bytes and cost a conflict on
every `git merge upstream/main`, which this fork is built to keep cheap.
(C++11 here, so `__attribute__((unused))` rather than `[[maybe_unused]]`.)

The unused **variables** are deleted outright, locals and file-scope
statics, small self-contained edits.

Two things are worth knowing about that script. Its edits are pinned to line
numbers *and* an expected substring, because matching on line text alone
silently breaks the build: `int rssi;`, `int right = r.x + r.w - 6;` and
`static unsigned long lastSpamTime = 0;` each appear verbatim in other
functions where the variable is read. And `PacketMonitor::draw()` turned out
to be dead in full (nothing calls it, and every line of its body wrote to a
local that was then discarded), so its body is now empty.

## What is still in the way of turning -w off

39 warnings in the sketch under `-Wall`:

| | count | |
|---|---|---|
| macro redefinition | 22 | always on, not `-Wall`-gated |
| `-Wformat-truncation=` | 14 | |
| `-Wdeprecated-declarations` | 2 | `tcpip_adapter_init` |
| lambda capture of static | 1 | always on |

The distinction matters. The 23 "always on" warnings are not produced by
`-Wall` at all. They appear at *any* warning level the moment `-w` comes
out. They are the real blocker to dropping `-w`, and `-Wall` is not what is
holding things up.

Both remaining pieces are now done. See "Scoping the UI constants" below.

### -w is off, -Wall is on

`tools/build.sh setup` now strips `-w` from `build.extra_flags.esp32` and the
normal build runs `--warnings more`, which is plain `-Wall`. **The sketch is
clean under it.**

The build filters warnings coming from TFT_eSPI and the ESP-IDF headers (35 of
them, repeated per translation unit) and prints a count instead. They are not
ours to fix, and a build that always prints noise is a build nobody reads.
`tools/build.sh warnings` still shows everything, and `-Wextra` is not the
default because 52 `-Wmissing-field-initializers` remain.

## Scoping the UI constants

The 22 redefinition warnings are gone, along with the `MAX_LINES` hazard.

Every one of the 154 definitions already sat inside a namespace or a function
body. These macros were being used as scoped constants by people who had no
scoped constants. So the fix was a straight swap:

```c
#define ICON_NUM 3          ->   constexpr int ICON_NUM = 3;
#define MAX_LINES (H / L)   ->   constexpr int MAX_LINES = (H / L);
```

Same names, so no use site changed and the diff is the definition lines only.
The derived ones now evaluate once, where they are written, which is what
removes the hazard: `MAX_LINES` can no longer re-resolve against a
`SCREEN_HEIGHT` from further down the file.

`STATUS_BAR_Y_OFFSET` needed one extra step. `shared.h` defines it as a macro
(default 0), and a live macro rewrites the declaration itself into
`constexpr int 0 = 20;`. Each site gets an `#undef` first. Uses above that
point still see the header's 0, exactly as before.

### Proving it changed nothing

A refactor that can silently resize an array needs more than "it compiles".
Three gates:

`tools/macro_value_check.py` recomputes every derived-macro use twice (with
the operand values in effect at the use, and with those in effect at the
definition), and compares. 13 uses, no differences. Had any differed, that
would have been a live bug the macros were hiding rather than a reason not to
proceed.

`tools/macro_containment_check.py` walks braces to find each definition's
block and checks that every use it governs falls inside. 511 uses, no escapes.
Worth noting its first run reported three, all false: two were the macro names
appearing in a comment (`subghz.cpp` has one that reads "Avoid jammer/replay
macros (SCREEN_WIDTH, ICON_NUM, …) leaking into this scope". Someone hit
this before and worked around it with `k`-prefixed locals), and one was brace
drift, verified by hand to be inside `namespace FirmwareUpdate`.

Then the real check: **all 14,157 symbols in the linked image are identical in
size before and after**, and the image is the same 1824493 bytes. The compiler
produced functionally identical output. No array resized, no loop bound moved,
no constant changed value.

### What is left

Nothing. The sketch is clean under `-Wall -Wextra`, and that is what
`tools/build.sh` runs.

Clearing the last 53 took four `= {0}` initializers and one real bug.

The 52 `-Wmissing-field-initializers` were all one idiom, in four places:

```c
wifi_ap_record_t ap_record = {0};   ->   = {}
wifi_config_t    ap_config = {0};   ->   = {}
```

Both spellings zero every member. `{0}` reads as "initialise the first field
and leave 25 alone", which is what `-Wextra` objects to; `{}` says initialise
all of them. Identical output.

The single `-Wtype-limits` was not cosmetic.

```c
static bool applyBrightness(uint8_t v){
  if (v > 255) v = 255;
```

`v` is a `uint8_t`, so the clamp can never fire. That is all the warning
says. The reason it is there is the interesting part:

```c
if (sel==0 && s.brightness<255) { applyBrightness(s.brightness+8); }
```

`s.brightness` is a `uint8_t`. `s.brightness + 8` promotes to `int`, and the
`uint8_t` parameter truncated it mod 256 at the call boundary, before the
clamp could see it. So brightness 248..254, all of which pass the caller's
`< 255` guard, arrived as 0..6, and pressing "brighter" near maximum dropped
the backlight to almost off.

The clamp was written to prevent exactly that and sat one scope too late to
do it. `applyBrightness` now takes an `int` and clamps both ends, which is
what it was always trying to be.

That is a behaviour change, unlike everything else in this pass: stepping up
from 250 now gives 255 instead of 2.

### Verification

The macro work and this pass together changed **one** symbol in the linked
image:

```
- 0000002e _ZN13AppSettingsUIL15applyBrightnessEh
+ 00000036 _ZN13AppSettingsUIL15applyBrightnessEi
```

The brightness fix. The other 14,156 symbols are identical in size, which is
what you want from a refactor: if anything else had moved, it would have been
a bug introduced rather than removed.

### Two suppressions, on purpose

`tcpip_adapter_init()` is wrapped in `#pragma GCC diagnostic ignored
"-Wdeprecated-declarations"` with a note. It is a shim on IDF 4.4 and gone in
IDF 5, the replacement is not a straight substitution, and it sits in the WiFi
bring-up path with no way to test it yet. On the list for any move to core
3.x.

The normal build filters warnings from TFT_eSPI and the ESP-IDF headers (40
of them, repeated per translation unit), and prints the count instead. They
are not ours to fix, and a build that always prints noise is a build nobody
reads. `tools/build.sh warnings` shows everything unfiltered.

## -Wunused-const-variable: tried, and not kept

Two constants in `bluetooth.cpp`, `SAMSUNG_COMPANY_ID` and
`GOOGLE_FAST_PAIR_ID`, were definitions with no readers, and `-Wall
-Wextra` said nothing, because `-Wunused-const-variable` is in neither for
C++. Adding it looked like a free win. It is not, and the reason is worth
writing down so the experiment is not repeated.

**At level 1 it catches nothing here.** The flag reaches the compile line,
verified by reading the actual command rather than the report, and it
works on a standalone translation unit compiled with the identical flag
list, warning on all four shapes of unused const. Put `SAMSUNG_COMPANY_ID`
back in `bluetooth.cpp` and rebuild, and it is silent. The same file warns
normally for `-Wunused-function`, so the file is being diagnosed; it is this
particular check that does not fire. Level 1 means "main file only", and
something about how the sketch is assembled appears to put these out of its
reach.

**At level 2 it catches everything.** Including `SAMSUNG_COMPANY_ID`, and
6,014 warnings in total, of which **1,410 are in our own files**: 1,181 in
`icon.h`, 141 in `shared.h`, 33 in `utils.h`. Those are headers defining
constants for whichever translation unit needs them, which is not the
mistake the flag was wanted for. Four real ones in `bluetooth.cpp` under
1,406 that are not is worse than none.

So the build stays `-Wall -Wextra`. The lesson is not about this flag:

**A warning-clean build is not a build with nothing unused in it.** Unused
`const` at namespace scope is invisible to it in C++, and an array written
through subscripts and never read, which is what the three spoofers do
with their randomised addresses, see `docs/pueo/spoofers.md`. Is invisible
to `-Wunused-but-set-variable`. Both were found by reading, and reading is
still the thing that finds them.
