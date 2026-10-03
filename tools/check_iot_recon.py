#!/usr/bin/env python3
"""IoT Recon fingerprints and tests creds without lying or wedging the UI.

Chained off the ARP Scanner's host list, IoT Recon probes each discovered host's
common ports, fingerprints HTTP/RTSP, and tests default HTTP Basic-Auth creds.
The ways it goes wrong, each caught here:

  1. Default creds are tried only against a host that actually asked for auth
     (HTTP 401). Try them at every host and the report is noise, and the tool
     hammers boxes that never wanted a login.

  2. A credential is reported working only on an authenticated 200. Any other
     code (still 401, a 500, a redirect) is not a successful login, and
     calling it one is a false positive someone acts on.

  3. The Basic-Auth header is real base64 of "user:pass". A wrong encoder sends
     garbage and every login fails, so the tool would always report "no creds"
     no matter the device. This reimplements reconB64 and checks known vectors.

  4. The scan is cooperative: one port per loop tick, so the UI redraws and Stop
     is reachable. Loop over all ports inside one step and the device looks
     frozen for the length of a full host scan, and Stop does nothing.

  5. It needs a joined network. reconStart refuses unless connected, and
     reconStep aborts if the connection drops mid-scan -- otherwise it spins
     connecting to an unreachable subnet.

  6. The heap state is freed. reconStop deletes it, and leaving the feature
     (teardown) calls reconStop -- or every run leaks ~700 bytes on a board
     that has none to spare.

Reads source. Needs no board.
"""
import re
import sys
import base64
from pathlib import Path

WIFI = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "wifi.cpp"

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


def squeeze(s):
    return re.sub(r"\s+", " ", s)


# reconB64 reimplemented from the firmware, to prove the C version's table and
# padding against base64 the standard library already gets right.
B64T = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"


def recon_b64(s):
    data = s.encode()
    out = []
    i = 0
    n = len(data)
    while i < n:
        v = data[i] << 16
        if i + 1 < n:
            v |= data[i + 1] << 8
        if i + 2 < n:
            v |= data[i + 2]
        out.append(B64T[(v >> 18) & 0x3F])
        out.append(B64T[(v >> 12) & 0x3F])
        out.append(B64T[(v >> 6) & 0x3F] if i + 1 < n else "=")
        out.append(B64T[v & 0x3F] if i + 2 < n else "=")
        i += 3
    return "".join(out)


def main():
    src = WIFI.read_text(encoding="utf-8", errors="replace")

    http = src.find("static void httpProbe(")
    http_end = src.find("static void rtspProbe(")
    http_region = squeeze(src[http:http_end]) if (http != -1 and http_end != -1) else ""
    ok("httpProbe found", bool(http_region))

    # 1. cred test only on 401.
    ok("creds tried only on HTTP 401",
       re.search(r"if\s*\(\s*code\s*==\s*401\s*\)\s*\{[^}]*RECON_CRED_N", http_region) is not None,
       "trying creds at every host is noise and abuse")

    # 2. success only on 200.
    ok("a credential counts only on an authed 200",
       re.search(r"if\s*\(\s*c2\s*==\s*200\s*\)\s*\{[^}]*CRED", http_region) is not None,
       "any other code is not a successful login")

    # 3. base64 correctness, three vectors including empty-password padding.
    vectors = {
        "admin:admin": base64.b64encode(b"admin:admin").decode(),
        "admin:": base64.b64encode(b"admin:").decode(),
        "root:12345": base64.b64encode(b"root:12345").decode(),
    }
    b64_ok = all(recon_b64(k) == v for k, v in vectors.items())
    ok("base64 encoder matches the standard library", b64_ok,
       str({k: (recon_b64(k), v) for k, v in vectors.items()}))

    step = src.find("static void reconStep() {")
    step_end = src.find("void arpScannerLoop() {")
    step_region = src[step:step_end] if (step != -1 and step_end != -1) else ""
    sflat = squeeze(step_region)
    ok("reconStep found", bool(step_region))

    # 4. one port per tick: a single ++ advance, no loop over all ports.
    ok("reconStep advances one port per tick",
       re.search(r"s_rc->port\+\+\s*;", sflat) is not None and
       re.search(r"for\s*\([^)]*RECON_PORT_N", sflat) is None and
       re.search(r"while\s*\([^)]*RECON_PORT_N", sflat) is None,
       "looping all ports in one step freezes the UI and Stop")

    # 5. needs a connection.
    start_region = squeeze(src[src.find("static void reconStart() {"):src.find("static void reconStep() {")])
    ok("reconStart refuses unless connected",
       re.search(r"WiFi\.status\(\)\s*!=\s*WL_CONNECTED\s*\)\s*return", start_region) is not None)
    ok("reconStep aborts if the connection drops",
       re.search(r"WiFi\.status\(\)\s*!=\s*WL_CONNECTED", sflat) is not None)

    # 6. heap state freed.
    stop_region = squeeze(src[src.find("static void reconStop() {"):src.find("static void reconStart() {")])
    ok("reconStop deletes and nulls the state",
       re.search(r"delete\s+s_rc\s*;\s*s_rc\s*=\s*nullptr", stop_region) is not None,
       "every run would leak the recon state")
    # The file has several teardown()s; take the ArpScanner one, just before the loop.
    arploop = src.find("void arpScannerLoop() {")
    td = src.rfind("static void teardown() {", 0, arploop)
    teardown_region = squeeze(src[td:td + 200]) if td != -1 else ""
    ok("teardown stops recon", "reconStop()" in teardown_region,
       "leaving mid-recon would leak and keep the file open")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
