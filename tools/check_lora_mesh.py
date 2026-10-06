#!/usr/bin/env python3
"""The private H.O.M.E LoRa mesh floods safely and encrypts.

A flood mesh without a seen-cache storms the air with duplicates forever; one
without a hop limit never stops; one that re-encrypts on relay corrupts the
message. And the whole point of a PRIVATE net is that the payload is encrypted
under a real (non-trivial) key. This pins all four so a later edit cannot quietly
remove the dedup, the hop decrement, the stable-counter rule, or weaken the key.
Reads source; needs no board.

    python tools/check_lora_mesh.py
"""
import re
import sys
from pathlib import Path

SK = Path(__file__).resolve().parent.parent / "ESP32-DIV"
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


def main():
    c = SK / "mesh.cpp"
    h = SK / "mesh.h"
    ok("mesh.cpp exists", c.is_file())
    ok("mesh.h exists", h.is_file())
    if not c.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    src = c.read_text(encoding="utf-8", errors="replace")

    print("flood control:")
    ok("has a seen-cache", "seenHas" in src and "seenAdd" in src)
    ok("unwrap() drops duplicates (seenHas guard returns 0)",
       re.search(r"if\s*\(\s*seenHas\([^)]*\)\s*\)\s*return\s+0", src) is not None)
    ok("unwrap() records new (src,id)",
       re.search(r"seenAdd\([^)]*\);", src) is not None)
    ok("relay only while hops remain (> 1)",
       re.search(r"hops\s*>\s*1", src) is not None)
    ok("relay decrements the hop count",
       re.search(r"hops\s*-\s*1", src) is not None)
    ok("a node ignores its own echo (src == myId)",
       re.search(r"src\s*==\s*s_myId", src) is not None)

    print("\nencryption:")
    ok("uses AES (mbedtls)", "mbedtls_aes" in src and "aes.h" in src)
    ok("CTR mode (keystream, relay-safe)", "crypt_ctr" in src)
    # counter derived from (src,id) -- stable across relays, which must NOT re-encrypt
    ok("counter is derived from src and id",
       re.search(r"nonce\[0\]\s*=.*src", src) is not None and
       re.search(r"nonce\[2\]\s*=.*id", src) is not None)
    # key must be a real 16-byte key, not all-zero / trivial
    m = re.search(r"HOME_KEY\[16\]\s*=\s*\{([^}]*)\}", src, re.S)
    ok("HOME_KEY is defined (16 bytes)", m is not None)
    if m:
        bytes_ = [b.strip() for b in m.group(1).split(",") if b.strip()]
        vals = []
        for b in bytes_:
            try:
                vals.append(int(b, 0))
            except ValueError:
                pass
        ok("HOME_KEY has 16 bytes", len(vals) == 16, "got %d" % len(vals))
        ok("HOME_KEY is not all-zero", any(v != 0 for v in vals))
        ok("HOME_KEY is not trivial (>=8 distinct bytes)",
           len(set(vals)) >= 8, "only %d distinct" % len(set(vals)))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
