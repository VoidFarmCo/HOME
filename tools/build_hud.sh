#!/usr/bin/env bash
# Build the Combat HUD (ESP32-C5) target. SEPARATE from tools/build.sh (H.O.M.E).
# Uses the GLOBAL arduino-cli core 3.3.x (NOT the .pueo-esp32 2.0.10 data dir).
#   build_hud.sh            -> compile
#   build_hud.sh upload COM5 -> upload to that port
set -euo pipefail
CLI="/c/Program Files/Arduino CLI/arduino-cli.exe"
ROOT="$(cd "$(dirname "$0")/.." && pwd)"
SKETCH="$ROOT/hud"
LIBS="$ROOT/hud/libraries"                       # vendored TFT_eSPI (Task 2)
# huge_app (3MB app) + 16M flash: BLE pushes the app past the default 1.25MB slice;
# the N16R8 has 16MB so we give the app room. (Changing the partition can reset NVS,
# i.e. the saved touch calibration, once.)
FQBN="esp32:esp32:esp32c5:PSRAM=enabled,CDCOnBoot=cdc,FlashSize=16M,PartitionScheme=huge_app"

LIBARG=()
[ -d "$LIBS" ] && LIBARG=(--libraries "$LIBS")   # only when the vendored dir exists

if [ "${1:-}" = "upload" ]; then
  "$CLI" upload -p "${2:-COM5}" --fqbn "$FQBN" "$SKETCH"
else
  "$CLI" compile --fqbn "$FQBN" --warnings all "${LIBARG[@]}" "$SKETCH"
fi
