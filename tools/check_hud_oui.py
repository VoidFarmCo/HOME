#!/usr/bin/env python3
"""Every OUI in hud/hud_oui.cpp belongs to the vendor its label names.

The table is generated from the IEEE registry; this re-checks it against the
cached registry so a hand-edit or a stale row can't put a wrong vendor on the
screen. A vendor hint is a claim about who built a radio, so it gets the same
authority (the IEEE MA-L list) the H.O.M.E spotter table does. The SCAN detail
view must actually use the lookup. No cache -> can't verify (benign, like
check_oui_registry): notes it and passes.
"""
import csv, io, re, sys
from pathlib import Path
ROOT = Path(__file__).resolve().parent.parent
CPP = (ROOT / "hud" / "hud_oui.cpp").read_text(encoding="utf-8", errors="replace")
PG  = (ROOT / "hud" / "hud_pages.cpp").read_text(encoding="utf-8", errors="replace")
CACHE = Path(__file__).resolve().parent / ".oui-cache.csv"
# label -> the registry-org pattern that label asserts (same shape the generator used)
PAT = {
    "DJI": r"\bdji\b", "PARROT": r"\bparrot\b", "SKYDIO": r"\bskydio\b", "AUTEL": r"\bautel\b",
    "ESPRESSIF": r"espressif", "RASPBERRYPI": r"raspberry", "MIKROTIK": r"mikrotik|routerboard",
    "UBIQUITI": r"ubiquiti", "APPLE": r"\bapple\b", "SAMSUNG": r"samsung", "GOOGLE": r"\bgoogle\b",
    "XIAOMI": r"xiaomi", "HUAWEI": r"huawei", "TP-LINK": r"tp-?link", "NETGEAR": r"netgear",
    "CISCO": r"\bcisco\b", "AMAZON": r"amazon", "MICROSOFT": r"microsoft", "INTEL": r"^intel\b",
    "REALTEK": r"realtek",
}
FAILED = []
def ok(n, c):
    print(("  ok    " if c else "  FAIL  ") + n)
    if not c: FAILED.append(n)
def main():
    ok("SCAN detail view uses the OUI vendor lookup",
       "hud_oui_vendor(c->bssid)" in PG)
    # parse the vendor label array (ordered) and the table rows
    vm = re.search(r"VENDORS\[\]\s*=\s*\{(.*?)\};", CPP, re.S)
    labels = re.findall(r'"([^"]+)"', vm.group(1)) if vm else []
    rows = re.findall(r"\{0x([0-9A-Fa-f]{6}),\s*(\d+)\}", CPP)
    ok("table + vendor list parsed (non-empty)", len(labels) >= 10 and len(rows) >= 50)
    ok("privacy (locally-administered) MACs are flagged, not vendor-guessed",
       "mac[0] & 0x02" in CPP and "RANDOMIZED" in CPP)
    if not CACHE.exists():
        print("  note  no registry cache (.oui-cache.csv) -- skipping per-OUI verify (benign)")
        print(); 
        if FAILED: print("FAILED: %d" % len(FAILED)); return 1
        print("all checks passed"); return 0
    reg = {}
    with io.open(CACHE, encoding="utf-8", errors="replace", newline="") as fh:
        for r in csv.DictReader(fh):
            a = (r.get("Assignment") or "").strip().upper()
            if len(a) == 6: reg[a] = (r.get("Organization Name") or "")
    bad = []
    for hexoui, vi in rows:
        a = hexoui.upper(); vi = int(vi)
        label = labels[vi] if vi < len(labels) else "?"
        org = reg.get(a)
        pat = PAT.get(label)
        if org is None or pat is None or not re.search(pat, org, re.I):
            bad.append("%s -> %s (registry: %s)" % (a, label, (org or "UNASSIGNED")[:30]))
    ok("every OUI verifies against the IEEE registry (%d rows)" % len(rows), not bad)
    for b in bad[:8]: print("          " + b)
    print()
    if FAILED: print("FAILED: %d" % len(FAILED)); return 1
    print("all checks passed"); return 0
if __name__ == "__main__": sys.exit(main())
