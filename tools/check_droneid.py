#!/usr/bin/env python3
"""Checks the Remote ID decoder in ESP32-DIV/DroneId.cpp.

Two things are being guarded, and they fail in opposite ways.

The scale factors fail *quietly*. Latitude is an int32 of ten-millionths,
altitude is a half-metre step with a 1000 m offset, horizontal speed is
either quarter-metres or three-quarter-metres depending on one bit in a
different byte. Every one of those produces a plausible number if you get it
wrong -- an aircraft at 4000 m instead of 1000 m, a drone doing 60 m/s
instead of 20. Nothing about the output says it is wrong. So the arithmetic
is transcribed here from the same source DroneId.cpp was written from, and
run against vectors built by this file rather than by the firmware.

The framing fails *loudly*, but only if it is reached. A message pack states
its own element size and count, and both come off the air: a pack claiming
nine 25-byte messages inside a frame holding two walks 175 bytes past the
end. Same for a Wi-Fi vendor element whose declared length exceeds the
frame. Those are fed in deliberately, behind a buffer that refuses any read
outside the frame -- the same approach as fuzz_ie_walk.py, for the same
reason.

Reads source and runs arithmetic; needs no board.

    python tools/check_droneid.py
"""
import random
import re
import struct
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SRC = REPO / "ESP32-DIV" / "DroneId.cpp"
HDR = REPO / "ESP32-DIV" / "DroneId.h"

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


# ── the reference arithmetic, transcribed from opendroneid-core-c ──────────
LATLON_MULT = 10000000.0
ALT_DIV, ALT_ADDER = 0.5, 1000.0
SPEED_DIV = (0.25, 0.75)
VSPEED_DIV = 0.5


def ref_latlon(enc):
    return enc / LATLON_MULT


def ref_alt(enc):
    return enc * ALT_DIV - ALT_ADDER


def ref_dir(enc, ew):
    return enc + 180.0 if ew else float(enc)


def ref_speed_h(enc, mult):
    if mult:
        return enc * SPEED_DIV[1] + 255 * SPEED_DIV[0]
    return enc * SPEED_DIV[0]


def ref_speed_v(enc):
    return enc * VSPEED_DIV


# ── a checked view of a frame, so an overrun is an error not a surprise ────
class Frame:
    def __init__(self, data):
        self.d = bytes(data)

    def __getitem__(self, i):
        if isinstance(i, slice):
            start, stop, _ = i.indices(len(self.d))
            if stop > len(self.d):
                raise IndexError("read past end")
            return self.d[i]
        if i < 0 or i >= len(self.d):
            raise IndexError("read past end at %d of %d" % (i, len(self.d)))
        return self.d[i]

    def __len__(self):
        return len(self.d)


# ── the decoder, transcribed ──────────────────────────────────────────────
MSG = 25
PACK_MAX = 9
VERSION = 2


def decode_message(f, off, out):
    if off + MSG > len(f):
        return False
    t = f[off] >> 4
    v = f[off] & 0x0F
    if v > VERSION:
        return False
    if t == 0x0:
        out["idType"] = f[off + 1] >> 4
        out["uaType"] = f[off + 1] & 0x0F
        raw = f[off + 2:off + 22]
        out["uasId"] = raw.split(b"\0")[0].decode("ascii", "replace")
        return True
    if t == 0x1:
        flags = f[off + 1]
        mult, ew = flags & 1, (flags >> 1) & 1
        out["status"] = (flags >> 4) & 0x0F
        d = f[off + 2]
        out["direction"] = -1.0 if d == 255 else ref_dir(d, ew)
        sh = f[off + 3]
        out["speedH"] = -1.0 if sh == 0xFF else ref_speed_h(sh, mult)
        sv = struct.unpack("<b", bytes([f[off + 4]]))[0]
        out["speedV"] = 0.0 if sv == 63 else ref_speed_v(sv)
        out["lat"] = ref_latlon(struct.unpack("<i", f[off + 5:off + 9])[0])
        out["lon"] = ref_latlon(struct.unpack("<i", f[off + 9:off + 13])[0])
        out["altGeo"] = ref_alt(struct.unpack("<H", f[off + 15:off + 17])[0])
        out["height"] = ref_alt(struct.unpack("<H", f[off + 17:off + 19])[0])
        ts = struct.unpack("<H", f[off + 21:off + 23])[0]
        out["ts"] = -1.0 if ts == 0xFFFF else ts / 10.0
        out["haveLocation"] = not (out["lat"] == 0.0 and out["lon"] == 0.0)
        return True
    if t == 0x4:
        out["opLat"] = ref_latlon(struct.unpack("<i", f[off + 2:off + 6])[0])
        out["opLon"] = ref_latlon(struct.unpack("<i", f[off + 6:off + 10])[0])
        out["haveOperator"] = not (out["opLat"] == 0.0 and out["opLon"] == 0.0)
        return True
    if t == 0x5:
        raw = f[off + 2:off + 22]
        out["operatorId"] = raw.split(b"\0")[0].decode("ascii", "replace")
        return True
    if t in (0x2, 0x3):
        return True
    return False


def decode_payload(f, off, length, out):
    if length < 1:
        return 0
    if (f[off] >> 4) != 0xF:
        return 1 if decode_message(f, off, out) else 0
    if length < 3:
        return 0
    single, count = f[off + 1], f[off + 2]
    if single != MSG:
        return 0
    if count == 0 or count > PACK_MAX:
        return 0
    if count * MSG + 3 > length:
        return 0
    n = 0
    for i in range(count):
        if decode_message(f, off + 3 + i * MSG, out):
            n += 1
    return n


def from_wifi_ie(f, out):
    if len(f) < 7 or f[0] != 0xDD:
        return 0
    declared = f[1]
    if declared + 2 > len(f) or declared < 5:
        return 0
    if bytes(f[2:5]) != b"\xFA\x0B\xBC" or f[5] != 0x0D:
        return 0
    return decode_payload(f, 7, declared - 5, out)


def from_ble(f, out):
    if len(f) < 3 or f[0] != 0x0D:
        return 0
    return decode_payload(f, 2, len(f) - 2, out)


# ── builders ──────────────────────────────────────────────────────────────
def basic_id(uas=b"PUEO-TEST-0001", idt=1, uat=2):
    m = bytearray(MSG)
    m[0] = (0x0 << 4) | VERSION
    m[1] = (idt << 4) | uat
    m[2:2 + len(uas)] = uas
    return bytes(m)


def location(lat, lon, alt_enc, spd, mult, ew, direction, sv, ts, status=2):
    m = bytearray(MSG)
    m[0] = (0x1 << 4) | VERSION
    m[1] = (status << 4) | (ew << 1) | mult
    m[2] = direction
    m[3] = spd
    m[4] = struct.pack("<b", sv)[0]
    m[5:9] = struct.pack("<i", lat)
    m[9:13] = struct.pack("<i", lon)
    m[15:17] = struct.pack("<H", alt_enc)
    m[17:19] = struct.pack("<H", alt_enc)
    m[21:23] = struct.pack("<H", ts)
    return bytes(m)


def wifi_wrap(payload):
    body = b"\xFA\x0B\xBC\x0D" + b"\x01" + payload
    return bytes([0xDD, len(body)]) + body


def ble_wrap(payload):
    return b"\x0D\x01" + payload


def pack(msgs):
    return bytes([(0xF << 4) | VERSION, MSG, len(msgs)]) + b"".join(msgs)


def main():
    print("the source states its constants once:")
    hdr = HDR.read_text(encoding="utf-8", errors="replace")
    src = SRC.read_text(encoding="utf-8", errors="replace")
    ok("header declares the ASD-STAN OUI FA:0B:BC",
       re.search(r"kWifiOui\[3\]\s*=\s*\{0xFA,\s*0x0B,\s*0xBC\}", hdr) is not None)
    ok("header declares vendor type 0x0D",
       "kWifiOuiType     = 0x0D" in hdr)
    ok("header declares BLE service UUID 0xFFFA",
       "kBleServiceUuid  = 0xFFFA" in hdr)
    ok("header declares BLE app code 0x0D", "kBleAppCode      = 0x0D" in hdr)
    ok("message size is 25", "kMessageSize    = 25" in hdr)
    ok("pack maximum is 9", "kPackMaxMessages = 9" in hdr)
    for name, val in (("kLatLonMult", "10000000.0"), ("kAltDiv", "0.5f"),
                      ("kAltAdder", "1000.0f"), ("kSpeedDiv0", "0.25f"),
                      ("kSpeedDiv1", "0.75f"), ("kVspeedDiv", "0.5f")):
        ok("%s is %s" % (name, val),
           re.search(re.escape(name) + r"\s*=\s*" + re.escape(val), src) is not None)

    print("\nthe arithmetic, against values with known answers:")
    ok("latitude 51.4778° round-trips", abs(ref_latlon(514778000) - 51.4778) < 1e-9)
    ok("longitude -0.0015° round-trips", abs(ref_latlon(-15000) - -0.0015) < 1e-9)
    ok("altitude encoding 2000 is 0 m", ref_alt(2000) == 0.0)
    ok("altitude encoding 0 is -1000 m", ref_alt(0) == -1000.0)
    ok("altitude encoding 2200 is 100 m", ref_alt(2200) == 100.0)
    ok("speed 80 at mult 0 is 20 m/s", ref_speed_h(80, 0) == 20.0)
    ok("speed 80 at mult 1 is 123.75 m/s", ref_speed_h(80, 1) == 123.75)
    ok("the mult bit is not a no-op", ref_speed_h(80, 0) != ref_speed_h(80, 1))
    ok("vertical speed -10 is -5 m/s", ref_speed_v(-10) == -5.0)
    ok("direction 90 east is 90", ref_dir(90, 0) == 90.0)
    ok("direction 90 west is 270", ref_dir(90, 1) == 270.0)

    print("\na built frame decodes to what was put in it:")
    out = {}
    n = from_wifi_ie(Frame(wifi_wrap(basic_id())), out)
    ok("Wi-Fi Basic ID decodes", n == 1 and out.get("uasId") == "PUEO-TEST-0001",
       repr(out.get("uasId")))
    out = {}
    loc = location(514778000, -15000, 2200, 80, 0, 0, 90, -10, 1234)
    n = from_ble(Frame(ble_wrap(loc)), out)
    ok("BLE Location decodes", n == 1)
    ok("  latitude", abs(out.get("lat", 0) - 51.4778) < 1e-7)
    ok("  altitude is 100 m", out.get("altGeo") == 100.0)
    ok("  ground speed is 20 m/s", out.get("speedH") == 20.0)
    ok("  climb is -5 m/s", out.get("speedV") == -5.0)
    ok("  heading is 90", out.get("direction") == 90.0)
    ok("  timestamp is 123.4 s", abs(out.get("ts", 0) - 123.4) < 1e-6)

    out = {}
    n = from_wifi_ie(Frame(wifi_wrap(pack([basic_id(), loc]))), out)
    ok("a message pack decodes both messages", n == 2)
    ok("  and merges them into one report",
       out.get("uasId") == "PUEO-TEST-0001" and out.get("altGeo") == 100.0)

    print("\nabsent fields are not reported as zero:")
    out = {}
    from_ble(Frame(ble_wrap(location(0, 0, 2000, 0xFF, 0, 0, 255, 63, 0xFFFF))), out)
    ok("0,0 is not treated as a position", out.get("haveLocation") is False)
    ok("speed 0xFF reports as unknown", out.get("speedH") == -1.0)
    ok("direction 255 reports as unknown", out.get("direction") == -1.0)
    ok("timestamp 0xFFFF reports as unknown", out.get("ts") == -1.0)

    print("\nframing that lies about its own length:")
    cases = [
        ("pack claims 9 messages, frame holds 1",
         wifi_wrap(bytes([(0xF << 4) | VERSION, MSG, 9]) + basic_id())),
        ("pack claims 0 messages",
         wifi_wrap(bytes([(0xF << 4) | VERSION, MSG, 0]))),
        ("pack claims 255 messages",
         wifi_wrap(bytes([(0xF << 4) | VERSION, MSG, 255]) + basic_id())),
        ("pack declares a 200-byte element size",
         wifi_wrap(bytes([(0xF << 4) | VERSION, 200, 2]) + basic_id() * 2)),
        ("vendor element longer than the frame",
         bytes([0xDD, 200, 0xFA, 0x0B, 0xBC, 0x0D, 0x01]) + basic_id()),
        ("vendor element shorter than its own header",
         bytes([0xDD, 2, 0xFA, 0x0B, 0xBC, 0x0D, 0x01])),
        ("truncated message, 10 bytes of 25",
         wifi_wrap(basic_id()[:10])),
        ("empty payload", wifi_wrap(b"")),
        ("wrong OUI", bytes([0xDD, 30, 0x00, 0x11, 0x22, 0x0D, 0x01]) + basic_id()),
        ("wrong vendor type",
         bytes([0xDD, 30, 0xFA, 0x0B, 0xBC, 0x99, 0x01]) + basic_id()),
        ("future protocol version",
         wifi_wrap(bytes([(0x0 << 4) | 15]) + bytes(24))),
    ]
    for name, frame in cases:
        try:
            from_wifi_ie(Frame(frame), {})
            ok("refused or contained: %s" % name, True)
        except IndexError as e:
            ok("refused or contained: %s" % name, False, "read past end (%s)" % e)

    print("\nsixty thousand random frames, none may read past the end:")
    rng = random.Random(0x0D0D)
    overruns = 0
    for _ in range(60000):
        n = rng.randrange(0, 80)
        f = Frame(bytes(rng.randrange(256) for _ in range(n)))
        for fn in (from_wifi_ie, from_ble):
            try:
                fn(f, {})
            except IndexError:
                overruns += 1
            except (struct.error, UnicodeDecodeError):
                overruns += 1
    ok("no random frame reads outside itself", overruns == 0,
       "%d overrun(s)" % overruns)

    # And the same frames with a valid header, so the walk is actually entered
    # rather than rejected at the first byte -- the mistake check_nav_labels
    # made, where a rule that never ran reported as passing.
    reached = 0
    overruns = 0
    for _ in range(60000):
        n = rng.randrange(0, 60)
        body = bytes(rng.randrange(256) for _ in range(n))
        frame = bytes([0xDD, min(len(body) + 5, 255), 0xFA, 0x0B, 0xBC, 0x0D,
                       0x01]) + body
        f = Frame(frame)
        try:
            if from_wifi_ie(f, {}) >= 0:
                reached += 1
        except IndexError:
            overruns += 1
        except (struct.error, UnicodeDecodeError):
            overruns += 1
    ok("valid-header fuzzing reaches the decoder", reached > 50000,
       "%d of 60000" % reached)
    ok("and still reads nothing outside the frame", overruns == 0,
       "%d overrun(s)" % overruns)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
