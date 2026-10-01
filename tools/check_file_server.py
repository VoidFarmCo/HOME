#!/usr/bin/env python3
"""File Transfer is read only, password protected, and shuts up afterwards.

Three sentences on the screen and in FileServer.h are load-bearing, and all
three are one edit away from being false while everything still compiles and
the feature still works:

  "Read only. Nothing here can write to the card."
      A delete button is the obvious next request. SD File Manager already
      has one, four lines of code away in the same menu, and the moment this
      grows one the sentence on the page becomes a lie told to whoever was
      handed the password.

  the password on screen
      WiFi.softAP(ssid) compiles exactly as well as WiFi.softAP(ssid, pass)
      and raises an open network. Nothing fails, nothing warns, the page
      works better than before -- and the wardrive log is now served to
      everyone in range. This is the single argument the whole feature's
      safety rests on.

  "Exit shuts the AP down."
      An exit that forgets leaves the device beaconing from a menu screen
      that gives no sign of it. Stealth Mode would read ON at the time.

And one that is not a sentence but a hole: every path this serves arrives
from the client. pathOk refuses traversal, and it is worth nothing if a new
route reaches SD.open without going through it.

    python tools/check_file_server.py

Reads source; needs no board.

What this does not cover
------------------------
The two escapings. A filename goes into the page twice, once as link text
and once inside a query value, and they need different treatment: htmlEscape
for the text, urlEncode for the href. Using one for both is a real bug and
this does not see it, because telling which sendContent is which means
reading the markup being assembled rather than the source assembling it.

It is a broken link rather than a hole -- a file called "a&b.csv" would
offer a download of "/logs/a" -- and no filename this firmware writes
contains a character either function changes. Worth knowing; not worth a
check that would go stale the first time the listing is reformatted.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "FileServer.cpp"

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


def bodies(src):
    """{name: body} for every function definition in the file.

    Brace matching rather than a regex to the closing brace at column 0:
    these are inside `namespace FileServer { namespace { ... } }`, so every
    one of them is indented and nothing closes at column 0 until the end of
    the file.
    """
    out = {}
    sig = re.compile(r"^[ \t]*(?:static\s+)?(?:const\s+)?[\w:*&<>]+[ \t*&]+"
                     r"(\w+)\s*\(", re.M)
    for m in sig.finditer(src):
        i = src.find("{", m.end() - 1)
        if i < 0:
            continue
        # a declaration or a call, not a definition
        if ";" in src[m.end() - 1:i]:
            continue
        depth = 0
        for k in range(i, len(src)):
            if src[k] == "{":
                depth += 1
            elif src[k] == "}":
                depth -= 1
                if depth == 0:
                    out[m.group(1)] = src[i:k + 1]
                    break
    return out


def main():
    src = SRC.read_text(encoding="utf-8")
    fns = bodies(src)

    print("nothing here writes to the card:")
    # SD File Manager is the one that writes, and it is reached with the panel
    # in your hand rather than over a radio by whoever has the password.
    for bad in ("SD.remove(", "SD.rmdir(", "SD.mkdir(", "SD.rename(",
                "FILE_WRITE", "FILE_APPEND", ".print(", ".println("):
        ok("no %s" % bad, bad not in src,
           "the page tells the client this cannot happen")
    ok("HTTP_POST and HTTP_PUT are not routed",
       "HTTP_POST" not in src and "HTTP_PUT" not in src,
       "a route that takes a body is a route that changes something")

    print("\nthe access point carries the generated password:")
    setup = fns.get("setup", "")
    ok("setup() found", bool(setup))
    m = re.search(r"WiFi\.softAP\(([^;]*?)\)\s*\)", setup) or \
        re.search(r"WiFi\.softAP\(([^;]*?)\)", setup)
    args = [a.strip() for a in m.group(1).split(",")] if m else []
    ok("softAP is called with a password",
       len(args) >= 2 and args[1] == "s_pass",
       "args are %r; the one-argument form raises an OPEN network" % (args,))
    # An 8 digit password is WPA2's minimum. A shorter one is not rejected by
    # this code, it is rejected by the stack at runtime -- softAP returns
    # false and the screen says "No SD card", which is a confusing way to
    # find out.
    m = re.search(r"kPassDigits\s*=\s*(\d+)", src)
    ok("the password is at least WPA2's 8 characters",
       m is not None and int(m.group(1)) >= 8,
       "kPassDigits is %s" % (m.group(1) if m else "missing"))
    ok("it is generated, not a constant",
       "esp_random()" in fns.get("makeCredentials", ""),
       "a password in the source is a password in the published binary")
    ok("a new one each time the screen opens",
       "makeCredentials()" in setup,
       "generated once at boot is a password that outlives the session")

    print("\nexit() takes the radio back down:")
    ex = fns.get("exit", "")
    ok("exit() found", bool(ex))
    ok("it disconnects the AP", "softAPdisconnect" in ex,
       "the AP outlives the screen that says it is running")
    ok("it closes the server", "close()" in ex)
    ok("it frees the server", "delete s_server" in ex,
       "a WebServer left allocated holds its listening socket")
    # exit() runs even when setup() refused under Stealth Mode, so every step
    # has to be safe on a feature that never started. The guard is what makes
    # that true.
    ok("and guards on having started", "if (s_apUp)" in ex,
       "exit() also runs when Stealth refused before anything came up")

    print("\nevery path from the client is checked before it reaches the card:")
    pathok = fns.get("pathOk", "")
    ok("pathOk() found", bool(pathok))
    ok("it refuses traversal", '".."' in pathok,
       "a path with .. in it is a path somebody built by hand")
    ok("it requires an absolute path", "'/'" in pathok)
    ok("it bounds the length", "kMaxPathLen" in pathok)
    # The Content-Disposition filename comes straight out of a path, so a
    # control character in one is a header the client reads as two.
    ok("it refuses control characters", "0x20" in pathok and "0x7F" in pathok,
       "a newline in a filename splits the response header")

    opened = [name for name, body in fns.items()
              if "SD.open(" in body and name != "pathOk"]
    ok("found the handlers that open the card", len(opened) >= 2,
       "%r" % (opened,))
    for name in sorted(opened):
        body = fns[name]
        gate = body.find("pathOk(")
        first = body.find("SD.open(")
        ok("  %s() checks first" % name, 0 <= gate < first,
           "SD.open at %d, pathOk at %d" % (first, gate))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
