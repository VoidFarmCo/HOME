#!/usr/bin/env bash
# Build the Halehound firmware with a pinned, isolated toolchain.
#
#   tools/build.sh setup    install core + libraries (once, ~1 GB)
#   tools/build.sh          compile
#   tools/build.sh upload COM7
#
# Nothing here touches a global Arduino install. The core lives under
# $HH_ARDUINO_ROOT (default ~/.hh-esp32) and the libraries under .arduino/user
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
HH_ARDUINO_ROOT="${HH_ARDUINO_ROOT:-$HOME/.hh-esp32}"

export ARDUINO_DIRECTORIES_DATA="$HH_ARDUINO_ROOT/data"
export ARDUINO_DIRECTORIES_USER="$REPO/.arduino/user"
export ARDUINO_DIRECTORIES_DOWNLOADS="$HH_ARDUINO_ROOT/downloads"
BUILD_PATH="$HH_ARDUINO_ROOT/build"

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
  "IRremoteESP8266@2.9.0"
  "Adafruit PN532@1.3.4"
  "PCF8574@0.4.5"
)

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
  # The first is load-bearing. The other two paper over problems rather than
  # fixing them and are worth revisiting once the fork settles.
  if [ ! -f "$CORE_DIR/platform.txt.orig" ]; then
    cp "$CORE_DIR/platform.txt" "$CORE_DIR/platform.txt.orig"
  fi
  cp "$REPO/Libraries/platform.txt" "$CORE_DIR/platform.txt"
  echo "== platform.txt patched (stock kept as platform.txt.orig) =="

  # TFT_eSPI and the CC1101 driver must come from the repo, not Library
  # Manager: upstream customised both.
  local LIB="$ARDUINO_DIRECTORIES_USER/libraries"
  echo "== repo libraries =="
  rm -rf "$LIB/TFT_eSPI" "$LIB/SmartRC-CC1101-Driver-Lib"
  unzip -q -o "$REPO/Libraries/TFT_eSPI-master.zip" -d "$LIB"
  mv "$LIB/TFT_eSPI-master" "$LIB/TFT_eSPI"
  unzip -q -o "$REPO/Libraries/SmartRC-CC1101-Driver-Lib-master.zip" -d "$LIB"
  mv "$LIB/SmartRC-CC1101-Driver-Lib-master" "$LIB/SmartRC-CC1101-Driver-Lib"
  cp "$REPO/Libraries/User_Setup cyd.h" "$LIB/TFT_eSPI/User_Setup.h"

  echo "== pinned libraries =="
  arduino-cli lib install "${LIBS[@]}"

  echo
  echo "setup complete. core: $CORE_DIR"
}

compile() {
  python "$REPO/tools/check_pinmap.py"
  echo
  arduino-cli compile -b "$FQBN" --build-path "$BUILD_PATH" "$REPO/ESP32-DIV"
}

upload() {
  local port="${1:?usage: tools/build.sh upload <port>}"
  arduino-cli upload -b "$FQBN" -p "$port" --input-dir "$BUILD_PATH" "$REPO/ESP32-DIV"
}

case "${1:-compile}" in
  setup)   setup ;;
  compile) compile ;;
  upload)  shift; upload "$@" ;;
  *) echo "usage: tools/build.sh [setup|compile|upload <port>]" >&2; exit 2 ;;
esac
