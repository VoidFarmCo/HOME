#!/usr/bin/env bash
# Build a source archive of Pueo, plus a merged flash image.
#
#   tools/make_release.sh            -> dist/pueo-<version>-src.zip
#   tools/make_release.sh --with-bin -> also dist/pueo-<version>-merged.bin
#
# The repo tracks 9 MB, down from 254. What is left is the firmware, the
# docs, the art, and the three files in Libraries/ that setup consumes.
#
# Removed along the way, all of it upstream's and none of it applicable to a
# CYD carrier design: PCB and schematic exports for their own boards, nine
# pre-compiled builds of their firmware, .elf and .map debug artifacts, the
# GLB board models, library zips for other board variants, their GitHub
# Pages site with its web flasher, and both flash tools. Everything is still
# in the upstream remote, recoverable with git show upstream/main:<path>.
#
# The archive is still an explicit include list rather than "everything
# except", because the list is the record of what belongs to this fork.
#
# Verifying one of these: extract it somewhere else, wipe the build tree, and
# build from scratch. The image should come back byte-identical, which is
# what -ffile-prefix-map in build.sh buys.
#
# Extract to a SHORT path -- C:\pv or similar -- and delete it afterwards.
# Somewhere deep like a temp directory fails: NimBLE's sources sit far enough
# down that a relative ../include/ resolves past Windows' 259-character
# limit, and the error is a header reported missing when it is right there.
# A junction from a short path to a deep one is not a way round it either:
# the compiler canonicalises through the junction, so the real path lands in
# the ELF's debug info, -ffile-prefix-map misses it, and the image quietly
# stops matching its published digest.
# None of that is needed to build the firmware and none of it is ours, so the
# archive is an explicit include list rather than "everything except".
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

VERSION=$(sed -n 's/^#define PUEO_VERSION *"\(.*\)"/\1/p' ESP32-DIV/Branding.h | head -1)
[ -n "$VERSION" ] || { echo "could not read PUEO_VERSION from Branding.h" >&2; exit 1; }

OUT="dist"
NAME="pueo-${VERSION}-src"
STAGE="$OUT/$NAME"

# What someone needs to build this, and nothing else.
#
# Libraries/ is trimmed to the three files setup actually consumes. The other
# zips there are for board variants Pueo does not target, and the "v1
# Libraries" folder is a duplicate set for the original ESP32-DIV hardware.
INCLUDE=(
  "ESP32-DIV"
  "libs"
  "docs/pueo"
  "art"
  "PUEO.md"
  "CHANGELOG.txt"
  "LICENSE"
  "LICENSE.MIT"
  ".gitignore"
  "tools/build.sh"
  "tools/make_release.sh"
  "tools/check_pinmap.py"
  "tools/fuzz_ie_walk.py"
  "tools/check_spotter_merge.py"
  "tools/check_spotter_capture.py"
  "tools/gen_netlist.py"
  "tools/make_bitmap.py"
  "tools/make_placeholder_logo.py"
  "Libraries/platform.txt"
  "Libraries/TFT_eSPI-master.zip"
  "Libraries/User_Setup cyd.h"
)

# The one-shot refactor scripts are history rather than tooling: each records
# how a specific change was made and refuses to run twice. Shipped under
# tools/history/ so the archive explains itself without implying they are
# things to run.
HISTORY=(
  "tools/drop_dead_functions.py"
  "tools/drop_ir_module.py"
  "tools/fix_format_truncation.py"
  "tools/macro_containment_check.py"
  "tools/macro_scope_report.py"
  "tools/macro_value_check.py"
  "tools/route_bus_claims.py"
  "tools/scope_ui_constants.py"
  "tools/silence_unused.py"
  "tools/tidy_scoped_constants.py"
)

rm -rf "$STAGE"
mkdir -p "$STAGE/tools/history"

for path in "${INCLUDE[@]}"; do
  [ -e "$path" ] || { echo "missing: $path" >&2; exit 1; }
  mkdir -p "$STAGE/$(dirname "$path")"
  cp -r "$path" "$STAGE/$(dirname "$path")/"
done

for path in "${HISTORY[@]}"; do
  [ -e "$path" ] && cp "$path" "$STAGE/tools/history/"
done

# Dropped by cp -r from art/, and not wanted in a source archive.
rm -rf "$STAGE/.arduino" "$STAGE/art"/*_render_*.png

cat > "$STAGE/BUILDING.txt" <<TXT
Pueo ${VERSION} - source archive

  tools/build.sh setup      one-time, installs a pinned toolchain (~1 GB)
  tools/build.sh            compile
  tools/build.sh merge      single flash image at offset 0
  tools/build.sh upload COM7

Requires arduino-cli and Python 3 with Pillow (only for the bitmap tools).

Everything is installed into its own root rather than a global Arduino
install, so it cannot disturb another project. Read the "Things that will
bite you" section of PUEO.md before deviating from the script -- the esp32
core version, the pinned library versions and the toolchain path length are
all load-bearing.

NOT TESTED ON HARDWARE. This compiles clean and much of it is verified at
the symbol level, but no part of it has been flashed to a board. See the
status note in PUEO.md.

tools/history/ holds the one-shot scripts that performed specific refactors,
kept for provenance. They are not meant to be run again and will refuse.
TXT

mkdir -p "$OUT"
# Git Bash on Windows has no zip(1); Python's zipfile is always there.
rm -f "$OUT/${NAME}.zip"
python -c "
import shutil, sys
shutil.make_archive(sys.argv[1], 'zip', root_dir=sys.argv[2], base_dir=sys.argv[3])
" "$OUT/${NAME}" "$OUT" "$NAME"
rm -rf "$STAGE"

SIZE=$(du -b "$OUT/${NAME}.zip" | cut -f1)
echo "$OUT/${NAME}.zip  ($(( SIZE / 1024 )) KB)"

if [ "${1:-}" = "--with-bin" ]; then
  bash tools/build.sh >/dev/null
  bash tools/build.sh merge >/dev/null
  BIN="${PUEO_ARDUINO_ROOT:-$HOME/.pueo-esp32}/build/pueo-merged.bin"
  cp "$BIN" "$OUT/pueo-${VERSION}-merged.bin"
  echo "$OUT/pueo-${VERSION}-merged.bin  ($(( $(du -b "$OUT/pueo-${VERSION}-merged.bin" | cut -f1) / 1024 )) KB)"
fi

( cd "$OUT" && sha256sum pueo-${VERSION}-* > "pueo-${VERSION}.sha256" )
echo
cat "$OUT/pueo-${VERSION}.sha256"
