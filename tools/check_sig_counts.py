#!/usr/bin/env python3
"""The documented signature count is the real one.

added.html said "one hundred and eight signatures over nine kinds" for
months after it stopped being true, and the user guide said nothing at all.
Nobody noticed, because a number in prose has nothing checking it and
reads as authoritative precisely because somebody once counted.

The same thing in a different shape as check_stealth.py's doc rules and
check_doc_versions.py's filenames: a fact copied out of the source, into a
place that does not compile.

    python tools/check_sig_counts.py

Reads source; needs no board.

The website
-----------
added.html lives in a separate repository, and this checks it when it can
find it and says nothing when it cannot. The path comes from
.publish.local, which make_release.sh already reads for the same reason, so
there is one place that knows where the site is.

Skipping silently is deliberate rather than lazy: this script ships inside
pueo-<version>-src.zip, where there is no site and no .publish.local, and a
release verification that failed because somebody else's website was not on
the disk would be a check about nothing.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "ESP32-DIV" / "SpotterSignatures.h"
GUIDE = ROOT / "docs" / "pueo" / "user-guide.md"
PUBLISH = ROOT / ".publish.local"

WORDS = {
    108: "one hundred and eight", 200: "two hundred",
    266: "two hundred and sixty-six", 267: "two hundred and sixty-seven",
    268: "two hundred and sixty-eight", 269: "two hundred and sixty-nine",
    270: "two hundred and seventy", 271: "two hundred and seventy-one",
    272: "two hundred and seventy-two", 273: "two hundred and seventy-three",
    274: "two hundred and seventy-four", 275: "two hundred and seventy-five",
    276: "two hundred and seventy-six", 277: "two hundred and seventy-seven",
    278: "two hundred and seventy-eight", 279: "two hundred and seventy-nine",
    280: "two hundred and eighty",
}

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


def count_rows():
    """Rows per table, comments and the table's own braces excluded."""
    src = SIGS.read_text(encoding="utf-8", errors="replace")
    out = {}
    for typ, name, body in re.findall(
            r"static const (\w+) (k\w+)\[\] = \{(.*?)\n\};", src, re.S):
        b = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        b = re.sub(r"//[^\n]*", "", b)
        out[name] = len(re.findall(r"^\s*\{", b, re.M))
    kinds = set(re.findall(r"Kind::(\w+)", src)) - {"Unknown"}
    return out, len(kinds)


def site_pages():
    """Every published page, if .publish.local says where the site is.

    Every page, not added.html alone. The first version checked that one
    because that is where the signature section lives, and firmware.html
    went on saying "108 signatures over nine kinds" with nothing looking at
    it. A number copied onto one page is usually copied onto two.
    """
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*[\"']?([^\"'\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    # .publish.local is read by a bash script and holds an MSYS path.
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    d = Path(raw)
    return sorted(d.glob("*.html")) if d.is_dir() else None


def main():
    tables, nkinds = count_rows()
    total = sum(tables.values())

    print("the tables:")
    for name, n in sorted(tables.items(), key=lambda kv: -kv[1]):
        print("    %-16s %3d" % (name, n))
    print("    %-16s %3d rows over %d kinds" % ("TOTAL", total, nkinds))
    print()

    ok("the count has a spelling", total in WORDS,
       "%d is not in WORDS; add it" % total)

    guide = GUIDE.read_text(encoding="utf-8", errors="replace")
    ok("the user guide states the count",
       re.search(r"\b%d signatures\b" % total, guide) is not None,
       "it does not say '%d signatures'" % total)
    ok("  and the number of kinds",
       re.search(r"\b%d kinds\b" % nkinds, guide) is not None,
       "it does not say '%d kinds'" % nkinds)

    # Every match type the guide's table claims. A new table in the header
    # with no row here is a kind of question the docs do not mention, which
    # is the half of this that matters: a reader cannot tell whether a thing
    # was missed or was never looked for.
    print()
    print("every table is described in the guide:")
    DESCRIBED = {
        "kMacSigs": "whole MAC address",
        "kOuiSigs": "first three bytes of a MAC",
        # Both anchored-name tables are one row in the guide: kSsidSigs is
        # WiFi and kBleNameSigs is BLE, and from the reader's side they are
        # the same question asked of two radios.
        "kSsidSigs": "from the start",
        "kBleNameSigs": "from the start",
        "kNameInSigs": "anywhere in it",
        "kBleSigs": "BLE company or service ID",
        "kBle128Sigs": "128-bit service UUID",
        "kMfgSigs": "manufacturer data",
        "kSvcDataSigs": "service data",
    }
    # Only the rows of the guide's own table, not the whole guide. "the
    # manufacturer data" also appears in the BLE Scanner section, so a
    # search over the file passed with the table row deleted -- the check
    # finding the phrase somewhere else entirely and calling it covered.
    m = re.search(r"#### What Surveillance is looking at(.*?)^####", guide,
                  re.S | re.M)
    rows = [l for l in (m.group(1) if m else "").splitlines()
            if l.startswith("|")]
    rows = "\n".join(rows)
    ok("the guide's table was found", bool(rows),
       "no markdown table under the Surveillance heading")

    for name in sorted(tables):
        phrase = DESCRIBED.get(name)
        ok("  %-14s" % name, phrase is not None and phrase in rows,
           "no row in the guide's table for it" if phrase is None
           else "no table row says %r" % phrase)

    print()
    pages = site_pages()
    if not pages:
        print("the site is not reachable; skipping it "
              "(no .publish.local, or no site there)")
    else:
        word = WORDS.get(total, "")
        html = "".join(p.read_text(encoding="utf-8", errors="replace")
                       for p in pages)
        print("checking %d published pages" % len(pages))
        ok("the site states the count",
           word and re.search(word, html, re.I) is not None,
           "no page says %r" % word)

        # And the digits, which is the spelling firmware.html uses. A number
        # written one way on one page and another way on another is still
        # one number, and both go stale together.
        for pg in pages:
            t = pg.read_text(encoding="utf-8", errors="replace")
            m2 = re.search(r"(\d+) signatures over", t)
            if m2:
                ok("  %s states %s" % (pg.name, total),
                   int(m2.group(1)) == total,
                   "it says %s signatures" % m2.group(1))
        # "two hundred" is a prefix of "two hundred and sixty-six", so a
        # plain substring search finds the shorter spelling inside the
        # correct one and reports the page as carrying two counts. The
        # lookahead is what makes a spelling match only when it is the whole
        # number rather than the start of a longer one.
        stale = [w for n, w in WORDS.items() if n != total
                 and re.search(w + r"(?!\s+and)", html, re.I)]
        ok("  and no longer says an older one", not stale,
           "it also says %s, so one of the two is wrong" % stale)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
