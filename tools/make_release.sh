#!/usr/bin/env bash
# Build a source archive of Pueo, plus a merged flash image.
#
#   tools/make_release.sh            -> dist/pueo-<version>-src.zip
#   tools/make_release.sh --with-bin -> also dist/pueo-<version>-merged.bin
#   tools/make_release.sh --force    -> re-cut a version already in dist/
#   tools/make_release.sh --no-publish -> cut into dist/ and copy nowhere
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

WITH_BIN=0
FORCE=0
# Cutting and publishing are two acts. .publish.local exists so that
# publishing does not need a path typed at it; this exists so that cutting
# does not need the file moved out of the way.
NO_PUBLISH=0
for arg in "$@"; do
  case "$arg" in
    --with-bin) WITH_BIN=1 ;;
    --force)    FORCE=1 ;;
    --no-publish) NO_PUBLISH=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

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
  "PueoBeacon"
  "docs/pueo"
  "PUEO.md"
  "CHANGELOG.txt"
  "LICENSE"
  "LICENSE.MIT"
  ".gitignore"
  "tools/build.sh"
  "tools/make_release.sh"
  "tools/check_pinmap.py"
  "tools/check_status_bar.py"
  "tools/check_droneid.py"
  "tools/check_beacon.py"
  "tools/fuzz_ie_walk.py"
  "tools/check_spotter_merge.py"
  "tools/check_spotter_capture.py"
  "tools/check_eapol.py"
  "tools/check_airtag_parse.py"
  "tools/check_sub_parse.py"
  "tools/check_fastpair.py"
  "tools/check_fastpair_probe.py"
  "tools/check_ble_adv.py"
  "tools/check_screen_dims.py"
  "tools/check_menu_dispatch.py"
  "tools/check_nav_labels.py"
  "tools/check_settings.py"
  "tools/check_stealth.py"
  "tools/trace_logo.py"
  "tools/inline_logo.py"
  "tools/check_logo_scale.py"
  "tools/check_menu_tables.py"
  "tools/check_tracker_follow.py"
  "tools/check_spotter_oui.py"
  "tools/render_screens.py"
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

# Refuse to quietly re-cut a version that has already been made.
#
# Rebuilding an existing version does not reproduce it once the tree has moved
# on, because this script is itself inside the archive: change anything and
# the zip's digest changes, while the one published beside it does not. The
# failure is silent and the symptom turns up much later, in somebody else's
# checksum.
#
# Bump PUEO_VERSION, or pass --force when the release has not gone anywhere.
if [ -f "$OUT/pueo-${VERSION}.sha256" ] && [ "$FORCE" != "1" ]; then
  echo "dist/ already holds $VERSION. Bump PUEO_VERSION in ESP32-DIV/Branding.h," >&2
  echo "or pass --force if that release has not been published anywhere." >&2
  exit 1
fi

# The include list is the record of what belongs to this fork, which only
# works while it is complete. It was not: check_screen_dims.py,
# check_menu_dispatch.py and check_nav_labels.py were written, used, relied
# on, and left out of every archive from 0.3.x to 0.4.2, so those releases
# ship a tree that cannot run its own checks.
#
# An explicit list is still right -- "everything except" is how the .scad
# files would have escaped -- but a list nobody diffs against reality is a
# list that drifts. This diffs it.
for f in tools/check_*.py; do
  case " ${INCLUDE[*]} " in
    *" $f "*) ;;
    *) echo "refusing to cut: $f is not in the archive include list" >&2
       MISSING_CHECKS=1 ;;
  esac
done
[ -z "${MISSING_CHECKS:-}" ] || exit 1

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

# Dragged in by cp -r, and not wanted in a source archive.
rm -rf "$STAGE/.arduino"

# The enclosure sources are not published. The STLs are what the site offers
# and what people print; the .scad files stay in the repository, where they
# are edited, and go no further. docs/pueo ships wholesale, so without this
# they would ride along in every archive and the decision would hold only on
# the website.
#
# Asserted rather than assumed: a file added to docs/pueo later should not
# quietly reinstate this.
rm -f "$STAGE"/docs/pueo/*.scad
if find "$STAGE" -name '*.scad' -print -quit | grep -q .; then
  echo "refusing to cut: a .scad reached the archive" >&2
  find "$STAGE" -name '*.scad' >&2
  exit 1
fi

cat > "$STAGE/BUILDING.txt" <<TXT
Pueo ${VERSION} - source archive

  tools/build.sh setup      one-time, installs a pinned toolchain (~1 GB)
  tools/build.sh            compile
  tools/build.sh merge      single flash image at offset 0
  tools/build.sh upload COM7

Builds for the 3.5" ESP32-3248S035R by default. For the 2.8" ESP32-2432S028R
put PUEO_PANEL=28 in front of every one of those:

  PUEO_PANEL=28 tools/build.sh
  PUEO_PANEL=28 tools/build.sh merge

The panels differ in the display driver, the backlight pin, CC1101's chip
select and which SPI bus touch is on. The wrong image is a dark screen
rather than an error.

Requires arduino-cli and Python 3 with Pillow (only for the bitmap tools).

Everything is installed into its own root rather than a global Arduino
install, so it cannot disturb another project. Read the "Things that will
bite you" section of PUEO.md before deviating from the script -- the esp32
core version, the pinned library versions and the toolchain path length are
all load-bearing.

Features inherited from upstream are inherited, not audited. See the status
note in PUEO.md.

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

# One image per CYD panel, because the two are not interchangeable: the
# display driver, the backlight pin, CC1101's chip select and which SPI bus
# the touch controller sits on all move between them. Flashing the wrong one
# is a dark screen, not an error message, so it is not a thing to let someone
# discover on their own board.
#
# The unsuffixed name keeps meaning what it has meant since 0.1.0 -- the 2.8"
# ESP32-2432S028R -- even though the tree itself now defaults to the 3.5".
# Repointing a filename whose digest is already published elsewhere is how a
# checksum starts failing for a reason nobody can reconstruct later.
#
# Build path comes from build.sh rather than being spelled again here; it
# moved with the panel once already.
if [ "$WITH_BIN" = "1" ]; then
  for panel in 28 35; do
    case "$panel" in
      28) suffix="";    label='2.8"' ;;
      35) suffix="-35"; label='3.5"' ;;
    esac
    out="$OUT/pueo-${VERSION}${suffix}-merged.bin"
    PUEO_PANEL="$panel" bash tools/build.sh >/dev/null
    PUEO_PANEL="$panel" bash tools/build.sh merge >/dev/null
    cp "$(PUEO_PANEL="$panel" bash tools/build.sh path)/pueo-merged.bin" "$out"
    echo "$out  ($(( $(du -b "$out" | cut -f1) / 1024 )) KB)  $label panel"
  done
fi

( cd "$OUT" && sha256sum pueo-${VERSION}-* > "pueo-${VERSION}.sha256" )
echo
cat "$OUT/pueo-${VERSION}.sha256"

# Optional publish step.
#
# PUEO_PUBLISH_DIR names a directory to copy the finished artifacts into: a
# website tree, a USB stick, wherever. Unset, this does nothing.
#
# It is an environment variable rather than a path written in here on
# purpose. This file ships inside the source archive, so a path from one
# machine would be published to everyone who downloads it -- which is the
# same mistake -ffile-prefix-map was added to stop the compiler making.
#
# It copies files and verifies the copies. It does not commit, push, or go
# near version control; whatever the destination is, publishing it stays a
# deliberate act somewhere else.
#
#   PUEO_PUBLISH_DIR=/path/to/site tools/make_release.sh --with-bin
#
# Typing that every time is how a release eventually gets published to the
# wrong directory, so .publish.local beside this repo is read first when it
# exists. That file is gitignored and is not in the include list above, so it
# never reaches the archive -- the path stays on the machine that owns it,
# which is the whole point of not writing one in here.
#
#   echo 'PUEO_PUBLISH_DIR=/path/to/site' > .publish.local
#
# An explicit PUEO_PUBLISH_DIR in the environment still wins.
if [ "$NO_PUBLISH" = "1" ]; then
  PUEO_PUBLISH_DIR=""
  echo
  echo "cut only; nothing published (--no-publish)"
elif [ -z "${PUEO_PUBLISH_DIR:-}" ] && [ -f "$REPO/.publish.local" ]; then
  # Only KEY=value lines, and only the one key. Sourcing a file to get a
  # string is how a stray command in it gets run.
  PUEO_PUBLISH_DIR=$(
    sed -n 's/^[[:space:]]*PUEO_PUBLISH_DIR[[:space:]]*=[[:space:]]*//p'         "$REPO/.publish.local" | tail -1 | sed 's/^["'"'"']//; s/["'"'"']$//'
  )
  [ -n "$PUEO_PUBLISH_DIR" ] && echo "publish dir from .publish.local: $PUEO_PUBLISH_DIR"
fi

if [ -n "${PUEO_PUBLISH_DIR:-}" ]; then
  DEST="$PUEO_PUBLISH_DIR"
  if [ ! -d "$DEST" ]; then
    echo "PUEO_PUBLISH_DIR is set but is not a directory: $DEST" >&2
    exit 1
  fi

  cp "$OUT/${NAME}.zip" "$DEST/"
  # Both panel images, each only if it was built -- a run without --with-bin
  # publishes the archive and the digests alone, as it always has.
  for img in "pueo-${VERSION}-merged.bin" "pueo-${VERSION}-35-merged.bin"; do
    [ -f "$OUT/$img" ] && cp "$OUT/$img" "$DEST/"
  done
  cp "$OUT/pueo-${VERSION}.sha256" "$DEST/"

  # The changelog goes under the name the site links, so the copy beside the
  # downloads and the copy inside the archive cannot drift apart.
  cp "$REPO/CHANGELOG.txt" "$DEST/pueo-changelog.txt"

  # Check what landed rather than trusting cp. A half-written binary beside a
  # correct digest is worse than no binary at all.
  ( cd "$DEST" && sha256sum -c "pueo-${VERSION}.sha256" ) || exit 1
  if ! cmp -s "$REPO/CHANGELOG.txt" "$DEST/pueo-changelog.txt"; then
    echo "changelog copy differs from the repo copy" >&2
    exit 1
  fi

  # ── retention ────────────────────────────────────────────────────────────
  #
  # Keep the release just cut, the one before it, and 0.2.1. Everything else
  # goes, so the download list stays short and the directory does not fill
  # with images nobody will flash.
  #
  # 0.2.1 is the exception because of the licence rather than the code: it is
  # the last release that still contained RF24, which is GPL-2.0-only and
  # could not share a binary with arduinoFFT or NimBLE. 0.2.2 is where that
  # ended. Anyone holding a 0.2.1 digest is holding the last of a distinct
  # licence state, so it stays reachable. See docs/pueo/licensing.md.
  #
  # Nothing is lost by pruning the rest: every release's digests are in
  # CHANGELOG.txt, which is published beside the downloads and ships inside
  # every archive.
  #
  # Only ever four exact filenames per version. No globbing over the
  # directory, because this runs against somebody's website tree. The -35
  # image exists only from 0.3.4 on; rm -f makes its absence a no-op for
  # every earlier version, and leaving the name out instead would make it
  # the one artefact that is never pruned.
  PUBLISH_KEEP_ALWAYS="0.2.1"

  published=$(ls "$DEST" 2>/dev/null \
    | sed -n 's/^pueo-\([0-9][0-9.]*\)\.sha256$/\1/p' | sort -V)
  keep=$(printf '%s\n' $published | tail -2)
  keep=$(printf '%s\n%s\n' "$keep" "$PUBLISH_KEEP_ALWAYS" | sort -V -u)

  pruned=0
  for v in $published; do
    if printf '%s\n' $keep | grep -qx -- "$v"; then
      continue
    fi
    rm -f "$DEST/pueo-$v-src.zip" \
          "$DEST/pueo-$v-merged.bin" \
          "$DEST/pueo-$v-35-merged.bin" \
          "$DEST/pueo-$v.sha256"
    echo "pruned $v"
    pruned=$((pruned + 1))
  done
  echo "keeping: $(printf '%s ' $keep)"
  [ "$pruned" -gt 0 ] && echo "($pruned older release(s) removed; their digests stay in CHANGELOG.txt)"

  echo
  echo "published to $DEST"
fi
