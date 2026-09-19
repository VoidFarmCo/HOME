#!/usr/bin/env bash
# Build the Pueo firmware with a pinned, isolated toolchain.
#
#   tools/build.sh setup    install core + libraries (once, ~1 GB)
#   tools/build.sh          compile
#   tools/build.sh upload COM7
#
# Nothing here touches a global Arduino install. The core lives under
# $PUEO_ARDUINO_ROOT (default ~/.pueo-esp32) and the libraries under .arduino/user
# in the repo.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Why the core does not live in the repo ───────────────────────────────────
# Windows MAX_PATH. Building with -fno-rtti (which the esp32 core does) selects
# the "no-rtti" libstdc++ multilib, and the resolved path to
#   .../xtensa-esp32-elf/include/c++/8.4.0/xtensa-esp32-elf/no-rtti/bits/error_constants.h
# came to 259 characters with the core inside the repo -- one under the 260
# limit, so the compiler reported the header as missing when it was right
# there. Everything else built. Keep this root short.
PUEO_ARDUINO_ROOT="${PUEO_ARDUINO_ROOT:-$HOME/.pueo-esp32}"

export ARDUINO_DIRECTORIES_DATA="$PUEO_ARDUINO_ROOT/data"
export ARDUINO_DIRECTORIES_USER="$REPO/.arduino/user"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PUEO_ARDUINO_ROOT/downloads"
BUILD_PATH="$PUEO_ARDUINO_ROOT/build"

# arduino-cli's winget install does not land on the Git Bash PATH.
if ! command -v arduino-cli >/dev/null 2>&1; then
  PATH="$PATH:/c/Program Files/Arduino CLI"
fi

CORE_VERSION="2.0.10"
ESP32_INDEX="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
CORE_DIR="$ARDUINO_DIRECTORIES_DATA/packages/esp32/hardware/esp32/$CORE_VERSION"

# CYD is a plain ESP32 dev module. min_spiffs buys a 1.9 MB app partition;
# the sketch currently sits at ~92% of it.
FQBN="esp32:esp32:esp32:PartitionScheme=min_spiffs"

# Library versions are pinned because several of these broke their APIs and
# Library Manager hands you the newest by default:
#   ArduinoJson 7      dropped StaticJsonDocument / createNestedObject
#   NimBLE 2.x         dropped NimBLEAdvertisedDeviceCallbacks, NimBLESecurity,
#                      renamed the NimBLEHIDDevice accessors
#   arduinoFFT 2.x     replaced the arduinoFFT class with ArduinoFFT<T>
LIBS=(
  "ArduinoJson@6.21.5"
  "NimBLE-Arduino@1.4.3"
  "arduinoFFT@1.6.2"
  "RF24@1.6.2"
  "rc-switch@2.6.4"
  "XPT2046_Touchscreen@1.4.0"
  "Adafruit PN532@1.3.4"
  "PCF8574@0.4.5"
)

# ── Why -zmuldefs is no longer needed ───────────────────────────────────────
# wifi.cpp defines ieee80211_raw_frame_sanity_check to return 0, overriding
# the IDF's copy so esp_wifi_80211_tx accepts hand-built frames. Without that
# the deauth and beacon features do nothing.
#
# The call is linker-resolved -- objdump shows .text.esp_wifi_80211_tx
# referencing the symbol through a literal-pool R_XTENSA_32 plus an
# ASM_EXPAND -- so the override does not need the whole link to tolerate
# duplicate symbols. Weakening the IDF's definition is enough: a strong
# definition beats a weak one, and everything else stays under normal
# duplicate-symbol rules.
#
# (nm --undefined-only does NOT show this reference, because the symbol is
# defined in the same object that calls it. Looking only at undefined imports
# suggests nothing calls it, which is wrong.)
#
# Verify after a build: the linked symbol should be 7 bytes, our `return 0`,
# not the IDF's ~200-byte original.
weaken_ieee80211_symbol() {
  local sdk="$CORE_DIR/tools/sdk/esp32/lib"
  local lib="$sdk/libnet80211.a"
  local objcopy
  objcopy=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/xtensa-esp32-elf-gcc/*/bin/xtensa-esp32-elf-objcopy.exe 2>/dev/null | head -1)
  [ -n "$objcopy" ] || { echo "objcopy not found" >&2; return 1; }
  [ -f "$lib.orig" ] || cp "$lib" "$lib.orig"
  cp "$lib.orig" "$lib"
  "$objcopy" --weaken-symbol=ieee80211_raw_frame_sanity_check "$lib"
  echo "== libnet80211.a: ieee80211_raw_frame_sanity_check weakened =="
}

setup() {
  mkdir -p "$ARDUINO_DIRECTORIES_DATA" "$ARDUINO_DIRECTORIES_USER/libraries" \
           "$ARDUINO_DIRECTORIES_DOWNLOADS"

  echo "== esp32 core $CORE_VERSION =="
  arduino-cli core update-index --additional-urls "$ESP32_INDEX"
  arduino-cli core install "esp32:esp32@$CORE_VERSION" --additional-urls "$ESP32_INDEX"

  # Upstream ships a patched platform.txt. The delta is three things:
  #   -DNFC_INTERFACE_SPI   puts the Adafruit PN532 library in SPI mode
  #   -zmuldefs             tells the linker to tolerate duplicate symbols
  #   -w                    silences every compiler warning
  #
  # Both -w and -zmuldefs are stripped below. See weaken_ieee80211_symbol and
  # the warning-flag block for why each can go.
  if [ ! -f "$CORE_DIR/platform.txt.orig" ]; then
    cp "$CORE_DIR/platform.txt" "$CORE_DIR/platform.txt.orig"
  fi
  cp "$REPO/Libraries/platform.txt" "$CORE_DIR/platform.txt"

  # Drop upstream's -w. It lived in build.extra_flags, which the compile
  # recipe appends *after* compiler.warning_flags, so it overrode whatever
  # --warnings asked for. Nothing needs hiding now.

  # Drop -zmuldefs too. weaken_ieee80211_symbol below makes the one duplicate
  # that was load-bearing resolve on its own, and without the blanket flag the
  # linker goes back to catching accidental duplicates -- which is how the
  # TFT_eSPI/CC1101 `spi` collision hid for so long.
  sed -i 's|^compiler\.c\.elf\.libs\.esp32=-zmuldefs |compiler.c.elf.libs.esp32=|'     "$CORE_DIR/platform.txt"
  sed -i 's|^build\.extra_flags\.esp32=-w |build.extra_flags.esp32=|' "$CORE_DIR/platform.txt"

  # The platform bakes -Werror=all into both raised levels, so `--warnings`
  # turned the first unused function into a failed build instead of a report.
  # Both builds use `all` (-Wall -Wextra); `more` is left as plain -Wall for
  # anyone who wants to drop -Wextra temporarily.
  sed -i 's|^compiler\.warning_flags\.more=.*|compiler.warning_flags.more=-Wall|' \
    "$CORE_DIR/platform.txt"
  sed -i 's|^compiler\.warning_flags\.all=.*|compiler.warning_flags.all=-Wall -Wextra|' \
    "$CORE_DIR/platform.txt"
  echo "== platform.txt patched (stock kept as platform.txt.orig) =="

  # TFT_eSPI and the CC1101 driver must come from the repo, not Library
  # Manager: upstream customised both.
  local LIB="$ARDUINO_DIRECTORIES_USER/libraries"
  echo "== repo libraries =="
  rm -rf "$LIB/TFT_eSPI" "$LIB/SmartRC-CC1101-Driver-Lib"
  unzip -q -o "$REPO/Libraries/TFT_eSPI-master.zip" -d "$LIB"
  mv "$LIB/TFT_eSPI-master" "$LIB/TFT_eSPI"
  cp "$REPO/Libraries/User_Setup cyd.h" "$LIB/TFT_eSPI/User_Setup.h"

  # The CC1101 driver is vendored rather than unzipped and sed'd. Its changes
  # are real source edits now -- see libs/SmartRC-CC1101-Driver-Lib/VENDORED.md.
  cp -r "$REPO/libs/SmartRC-CC1101-Driver-Lib" "$LIB/"

  weaken_ieee80211_symbol

  echo "== pinned libraries =="
  arduino-cli lib install "${LIBS[@]}"

  echo
  echo "setup complete. core: $CORE_DIR"
}

# __FILE__ ends up in the firmware. NimBLE's assert macros put the absolute
# path of every asserting source file into the image, which is how the build
# machine's home directory came to be inside the published 0.1.0 and 0.2.0
# binaries -- seventeen strings of it, including the old name of the
# workspace folder. -fmacro-prefix-map rewrites the prefix while
# preprocessing, so __FILE__ comes out under pueo/ and arduino/ instead.
#
# -ffile-prefix-map rather than -fmacro-prefix-map: the macro form rewrites
# __FILE__ only, which cleans the firmware but leaves the absolute paths in
# the ELF's debug info. The app descriptor carries a SHA-256 of that ELF, so
# two builds of identical code at different paths still produced different
# images. The file form covers debug info too, and with it the merged image
# is identical wherever it was built.
#
# Passed per build rather than patched into platform.txt on purpose:
# platform.txt lives in the shared core directory, while these two paths
# belong to this checkout. Baking them in there would leak one checkout's
# path into another checkout's build.
#
# Each root is mapped in all three spellings it can arrive in. The toolchain
# is a MinGW build and __FILE__ preserves whatever arduino-cli handed the
# compiler, which today is the backslashed C: form.
prefix_maps() {
  local flags="" root tag win mixed
  for root in "$REPO" "$PUEO_ARDUINO_ROOT"; do
    if [ "$root" = "$REPO" ]; then tag="pueo"; else tag="arduino"; fi
    win="$(cygpath -w "$root" 2>/dev/null || echo "$root")"
    mixed="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
    flags="$flags -ffile-prefix-map=$win=$tag"
    flags="$flags -ffile-prefix-map=$mixed=$tag"
    flags="$flags -ffile-prefix-map=$root=$tag"
  done
  echo "$flags"
}

# -Wall -Wextra, and the sketch is expected to stay clean under both. If this
# prints a warning, that is the whole point -- fix it rather than lowering the
# level again.
#
# Warnings from TFT_eSPI and the ESP-IDF headers are filtered out. They are
# not ours to fix, they repeat once per translation unit, and a build that
# always prints noise is a build nobody reads. `tools/build.sh warnings`
# shows everything.
compile() {
  python "$REPO/tools/check_pinmap.py"
  echo
  local log="$PUEO_ARDUINO_ROOT/compile.log"
  mkdir -p "$PUEO_ARDUINO_ROOT"
  local rc=0
  local maps; maps="$(prefix_maps)"
  arduino-cli compile --warnings all -b "$FQBN" \
    --build-property "compiler.c.extra_flags=$maps" \
    --build-property "compiler.cpp.extra_flags=$maps" \
    --build-path "$BUILD_PATH" "$REPO/ESP32-DIV" >"$log" 2>&1 || rc=$?

  grep -E "ESP32-DIV[\\/][A-Za-z_]+\.(cpp|h|ino).*(warning|error):" "$log" || true
  grep -E "^(Sketch uses|Global variables)" "$log" || true

  local ours external
  ours=$(grep -cE "ESP32-DIV[\\/][A-Za-z_]+\.(cpp|h|ino).*warning:" "$log" || true)
  external=$(( $(grep -c "warning:" "$log" || true) - ours ))
  if [ "$ours" -eq 0 ]; then
    echo "sketch is -Wall -Wextra clean ($external library/core warnings filtered)"
  else
    echo "$ours sketch warning(s) above -- these are ours"
  fi

  if [ "$rc" -ne 0 ]; then
    grep -E "error:|Error during build" "$log" | head -20
    return "$rc"
  fi
}

# Same warning level as the normal build, but nothing filtered: library and
# core warnings included. Use it when a warning is suspected to come from a
# library rather than the sketch.
warnings() {
  rm -rf "$BUILD_PATH-warnings"
  local maps; maps="$(prefix_maps)"
  arduino-cli compile --warnings all -b "$FQBN" \
    --build-property "compiler.c.extra_flags=$maps" \
    --build-property "compiler.cpp.extra_flags=$maps" \
    --build-path "$BUILD_PATH-warnings" "$REPO/ESP32-DIV" 2>&1 \
    | grep -E "warning:|Sketch uses|Global variables"
}

# Single flash image at offset 0: bootloader + partition table + app.
# This is what QEMU wants as its flash drive, and it is also the one-file
# artifact to hand someone who just wants to flash the thing.
merge() {
  local esptool
  esptool=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
  [ -n "$esptool" ] || { echo "esptool not found; run setup first" >&2; return 1; }
  "$esptool" --chip esp32 merge_bin -o "$BUILD_PATH/pueo-merged.bin"     --flash_mode dio --flash_freq keep --flash_size 4MB     0x1000  "$BUILD_PATH/ESP32-DIV.ino.bootloader.bin"     0x8000  "$BUILD_PATH/ESP32-DIV.ino.partitions.bin"     0x10000 "$BUILD_PATH/ESP32-DIV.ino.bin"
  echo "merged image: $BUILD_PATH/pueo-merged.bin"
}

upload() {
  local port="${1:?usage: tools/build.sh upload <port>}"
  arduino-cli upload -b "$FQBN" -p "$port" --input-dir "$BUILD_PATH" "$REPO/ESP32-DIV"
}

case "${1:-compile}" in
  setup)    setup ;;
  compile)  compile ;;
  warnings) warnings ;;
  merge)    merge ;;
  upload)   shift; upload "$@" ;;
  *) echo "usage: tools/build.sh [setup|compile|warnings|merge|upload <port>]" >&2; exit 2 ;;
esac
