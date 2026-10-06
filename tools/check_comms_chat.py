#!/usr/bin/env python3
"""The multi-channel chat: every transport implements the interface and is wired.

Chat is one UI (chat_core) over interchangeable radios. Each transport namespace
MUST provide the same six entry points, chat_core MUST dispatch every one of them
by channel, and the Chat tile MUST list every registered channel. A transport
that is registered but missing (say) send() would compile as a dead channel and
silently drop messages -- this pins the contract so that cannot happen. Reads
source; needs no board.

    python tools/check_comms_chat.py
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SK = ROOT / "ESP32-DIV"

# Active (compiled) transports. All three link on this board.
TRANSPORTS = {
    "chat_subghz.cpp": "SubghzChat",
    "chat_espnow.cpp": "EspNowChat",
    "chat_lora.cpp":   "LoRaChat",
}
IFACE = ["available", "init", "deinit", "send", "poll", "label"]

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


def iface_defined(src, call):
    return re.search(r"\b%s\b[^;{]*\{" % call, src) is not None


def main():
    core_c = SK / "chat_core.cpp"
    ok("chat_core.h exists", (SK / "chat_core.h").is_file())
    ok("chat_core.cpp exists", core_c.is_file())
    if not core_c.is_file():
        print("\nFAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    core = core_c.read_text(encoding="utf-8", errors="replace")
    ino = (SK / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")

    print("each active transport implements the six-call interface:")
    for fn, ns in TRANSPORTS.items():
        p = SK / fn
        ok("%s exists" % fn, p.is_file())
        if not p.is_file():
            continue
        src = p.read_text(encoding="utf-8", errors="replace")
        for call in IFACE:
            ok("%s::%s() defined" % (ns, call), iface_defined(src, call),
               "missing %s" % call)

    print("\nchat_core dispatches every interface call for every channel:")
    for ns in TRANSPORTS.values():
        for call in IFACE:
            ok("core dispatches %s::%s" % (ns, call),
               re.search(r"\b%s::%s\b" % (ns, call), core) is not None)
    ok("core builds a name+text frame", "nameLen" in core or "CHAT_NAME_MAX" in core)

    print("\nthe Chat tile lists channels and launches the core:")
    ok("launchChatFeature() exists", "launchChatFeature" in ino)
    ok("toolRun routes a chat category to it",
       re.search(r"case\s+6\s*:\s*launchChatFeature", ino) is not None)
    ok("more than one chat channel is registered",
       len(re.findall(r"GRP_CHAT,\s*\w+,\s*6,", ino)) >= 2)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
