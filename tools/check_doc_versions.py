#!/usr/bin/env python3
"""A download filename in the docs names the release being shipped.

The build guide told you to flash pueo-0.3.4-merged.bin, five releases after
0.3.4, and then told 3.5" owners to flash pueo-0.3.4-35-merged.bin instead
-- a file that does not exist and a sentence that is now backwards, because
the suffix reversed at 0.4.13. The plain name meant the 2.8" board until
then and means the 3.5" now.

The site's version references get swept by hand every release. The
firmware's docs never did, so the one place with a copy-and-paste command in
it drifted furthest, and the command was wrong in the way that wastes the
most of somebody's evening: a filename that 404s, followed by advice to use
a second filename that also 404s, in a guide they are following because they
have not soldered anything yet and want a known-good starting point.

    python tools/check_doc_versions.py

Reads source; needs no board.

Why filenames and not every version number
------------------------------------------
Because most version numbers in these docs are history and belong there.
"supported up to 0.4.13", "under a tile called More until 0.4.11", "shipped
in 0.4.16" -- every one of those is a fact about the past that would have to
be exempted, and a check that is mostly exemptions asserts nothing.

A download filename is never history. Nobody writes pueo-0.3.4-merged.bin to
describe what happened in 0.3.4; it is there to be copied into a terminal,
which makes the current version the only correct value and makes this
checkable without a list.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
BRANDING = REPO / "ESP32-DIV" / "Branding.h"

# pueo-<version>[-beacon-35]-merged.bin, and the source archive.
ARTEFACT = re.compile(r"pueo-(\d+\.\d+\.\d+)[a-z0-9-]*?"
                      r"(?:-merged\.bin|-src\.zip|\.sha256)")

DOCS = ["README.md", "PUEO.md", "CONTRIBUTING.md"]


def main():
    m = re.search(r'PUEO_VERSION\s+"([^"]+)"',
                  BRANDING.read_text(encoding="utf-8", errors="replace"))
    if not m:
        print("FAIL  PUEO_VERSION not found in Branding.h")
        return 1
    current = m.group(1)

    files = [REPO / d for d in DOCS if (REPO / d).is_file()]
    files += sorted((REPO / "docs").rglob("*.md"))

    stale = []
    seen = 0
    for path in files:
        text = path.read_text(encoding="utf-8", errors="replace")
        for mm in ARTEFACT.finditer(text):
            seen += 1
            if mm.group(1) == current:
                continue
            line = text[:mm.start()].count("\n") + 1
            stale.append("%s:%d  %s"
                         % (path.relative_to(REPO).as_posix(), line,
                            mm.group(0)))

    # CHANGELOG.txt is deliberately excluded: its whole job is to name old
    # releases, and its digest table names every artefact ever published.
    print("current version: %s" % current)
    print("download filenames found in the docs: %d" % seen)

    if seen == 0:
        print()
        print("FAIL: none found. The filename pattern stopped matching, and")
        print("      this is reporting green over nothing.")
        return 1

    print()
    if stale:
        for s in stale:
            print("  FAIL  " + s)
        print()
        print("FAILED: %d filename(s) name a release that is not this one."
              % len(stale))
        print()
        print("These get copied into a terminal. A stale one 404s, and the")
        print("reader is following the guide precisely because they do not")
        print("yet know what a working board looks like.")
        return 1

    print("every one of them names %s" % current)
    return 0


if __name__ == "__main__":
    sys.exit(main())
