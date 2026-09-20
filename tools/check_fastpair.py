"""Checks the Fast Pair advertisement parser in ESP32-DIV/FastPair.cpp.

The case this exists for: a Fast Pair payload has two shapes that share a
service UUID and nothing else. Three bytes is a Model ID and means the
device is in pairing mode. Anything else is a not-discoverable frame --
flags, an account key filter, maybe salt and battery -- and has no Model ID
in it anywhere.

Reading data[0..2] of a not-discoverable frame as a Model ID is the mistake
worth guarding, because it does not fail loudly. It produces a plausible
six-hex-digit number for a device that never advertised one, and most
devices in the air are in that state, because most earbuds already belong to
somebody.

    python tools/check_fastpair.py
"""
NONE = "None"
MODEL = "ModelId"
ACCOUNT = "AccountKey"
EMPTY = "Empty"

MAX_FILTER = 16
LEVEL_UNKNOWN = 0xFF

T_FILTER_SHOW = 0x0
T_SALT = 0x1
T_FILTER_HIDE = 0x2
T_BATT_SHOW = 0x3
T_BATT_HIDE = 0x4


def parse(data):
    """FastPair::parse(), transcribed."""
    out = dict(frame=NONE, modelId=0, version=0, flags=0, filterLen=0,
               filterTruncated=False, filterHideUi=False, filter=b"",
               haveSalt=False, salt=0,
               battery=dict(present=False, hideUi=False, count=0,
                            level=[LEVEL_UNKNOWN] * 3, charging=[False] * 3))
    if not data:
        return False, out

    if len(data) == 3:
        out["frame"] = MODEL
        out["modelId"] = (data[0] << 16) | (data[1] << 8) | data[2]
        return True, out

    out["version"] = data[0] >> 4
    out["flags"] = data[0] & 0x0F
    if out["version"] != 0:
        return False, out

    if len(data) == 1:
        out["frame"] = EMPTY
        return True, out

    pos = 1
    saw_filter = False
    while pos < len(data):
        header = data[pos]
        flen = header >> 4
        ftype = header & 0x0F
        if pos + 1 + flen > len(data):
            break                       # overruns the packet; keep what we had
        body = data[pos + 1:pos + 1 + flen]
        pos += 1 + flen

        if ftype in (T_FILTER_SHOW, T_FILTER_HIDE):
            saw_filter = True
            out["filterHideUi"] = ftype == T_FILTER_HIDE
            keep = min(flen, MAX_FILTER)
            if flen > MAX_FILTER:
                out["filterTruncated"] = True
            out["filter"] = body[:keep]
            out["filterLen"] = keep
        elif ftype == T_SALT:
            if flen == 1:
                out["haveSalt"] = True
                out["salt"] = body[0]
            elif flen == 2:
                out["haveSalt"] = True
                out["salt"] = (body[0] << 8) | body[1]
        elif ftype in (T_BATT_SHOW, T_BATT_HIDE):
            if flen > 0:
                b = out["battery"]
                b["present"] = True
                b["hideUi"] = ftype == T_BATT_HIDE
                b["count"] = min(flen, 3)
                for i in range(b["count"]):
                    b["charging"][i] = bool(body[i] & 0x80)
                    lvl = body[i] & 0x7F
                    b["level"][i] = LEVEL_UNKNOWN if lvl > 100 else lvl

    out["frame"] = ACCOUNT if (saw_filter and out["filterLen"] > 0) else EMPTY
    return True, out


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


def field(type_, body):
    assert len(body) <= 15
    return bytes([(len(body) << 4) | type_]) + bytes(body)


# --- discoverable: the three-byte Model ID ---------------------------------
ok, p = parse(bytes([0x2B, 0x71, 0xB2]))
case("three bytes is a Model ID", ok and p["frame"] == MODEL)
case("Model ID is big-endian 24-bit", p["modelId"] == 0x2B71B2)
case("a model frame has no filter", p["filterLen"] == 0)
case("a model frame has no battery", not p["battery"]["present"])

ok, p = parse(bytes([0x00, 0x00, 0x00]))
case("all-zero Model ID still parses", ok and p["frame"] == MODEL
     and p["modelId"] == 0)
ok, p = parse(bytes([0xFF, 0xFF, 0xFF]))
case("all-ones Model ID still parses", p["modelId"] == 0xFFFFFF)

# --- not-discoverable: the shape that must NOT become a Model ID -----------
NOTDISC = bytes([0x00]) + field(T_FILTER_SHOW, b"\x01\x02\x03\x04") \
          + field(T_SALT, b"\xC7")
ok, p = parse(NOTDISC)
case("a filter frame parses", ok and p["frame"] == ACCOUNT)
case("and reports no Model ID", p["modelId"] == 0)
case("filter bytes kept", p["filter"] == b"\x01\x02\x03\x04")
case("filter length", p["filterLen"] == 4)
case("salt kept", p["haveSalt"] and p["salt"] == 0xC7)
case("version parsed", p["version"] == 0)

# the mistake, stated as a test: the first three bytes of that frame are
# 0x00 0x40 0x01, which would print as Model ID 004001 and mean nothing.
case("the naive read would have invented a Model ID",
     (NOTDISC[0] << 16 | NOTDISC[1] << 8 | NOTDISC[2]) == 0x004001
     and p["modelId"] != 0x004001)

ok, p = parse(bytes([0x00]) + field(T_FILTER_HIDE, b"\xAA\xBB\xCC\xDD"))
case("hide-UI filter type recognised", p["frame"] == ACCOUNT
     and p["filterHideUi"])

# --- empty / no account keys -----------------------------------------------
ok, p = parse(bytes([0x00]))
case("a lone flags byte is Empty", ok and p["frame"] == EMPTY)
ok, p = parse(bytes([0x00]) + field(T_FILTER_SHOW, b""))
case("a zero-length filter is Empty", p["frame"] == EMPTY)
ok, p = parse(bytes([0x00]) + field(T_SALT, b"\x11") + field(T_SALT, b"\x22"))
case("salt with no filter is Empty", p["frame"] == EMPTY and p["haveSalt"])

# --- the one ambiguity the format has, stated rather than hidden -----------
# A not-discoverable frame holding a flags byte and a single one-byte field
# is three bytes long, which is indistinguishable from a Model ID. No bit
# anywhere separates them, so the length rule wins and such a frame reads as
# a Model ID.
#
# It does not arise in practice. The only one-byte fields are a salt and a
# one-component battery; a salt is meaningless without the account key
# filter it salts, and a battery is only sent alongside one. Both real
# shapes carry a filter and are therefore six bytes or longer. Recorded
# here so a future reader finds it measured rather than discovering it.
ok, p = parse(bytes([0x00]) + field(T_SALT, b"\x11"))
case("a three-byte not-discoverable frame reads as a Model ID",
     p["frame"] == MODEL and p["modelId"] == 0x001111)
case("and its length is why",
     len(bytes([0x00]) + field(T_SALT, b"\x11")) == 3)

# --- battery ---------------------------------------------------------------
ok, p = parse(bytes([0x00]) + field(T_FILTER_SHOW, b"\x01\x02\x03\x04")
              + field(T_BATT_SHOW, bytes([0x40, 0xC8, 0x7F])))
b = p["battery"]
case("battery present", b["present"] and b["count"] == 3)
case("left bud 64%, not charging", b["level"][0] == 64 and not b["charging"][0])
case("right bud 72%, charging", b["level"][1] == 72 and b["charging"][1])
case("case unknown", b["level"][2] == LEVEL_UNKNOWN)
case("battery does not disturb the filter", p["filterLen"] == 4)

ok, p = parse(bytes([0x00]) + field(T_BATT_HIDE, bytes([0x32, 0xC8])))
case("hide-UI battery recognised", p["battery"]["hideUi"])
case("two components", p["battery"]["count"] == 2
     and p["battery"]["level"][0] == 50 and not p["battery"]["charging"][0]
     and p["battery"]["level"][1] == 72 and p["battery"]["charging"][1])

ok, p = parse(bytes([0x00]) + field(T_BATT_SHOW, bytes([0x65, 0x66, 0x70])))
case("levels above 100 are unknown, not printed",
     all(v == LEVEL_UNKNOWN for v in p["battery"]["level"]))

ok, p = parse(bytes([0x00]) + field(T_BATT_SHOW, bytes([1, 2, 3, 4, 5])))
case("more than three components is clamped", p["battery"]["count"] == 3)

# --- bounds ----------------------------------------------------------------
ok, p = parse(bytes([0x00]) + field(T_FILTER_SHOW, bytes(range(15))))
case("a 15-byte filter fits", p["filterLen"] == 15 and not p["filterTruncated"])
case("filter stored intact", p["filter"] == bytes(range(15)))

# a field header claiming more than the packet holds
ok, p = parse(bytes([0x00, 0xF0, 0x01, 0x02]))
case("overrunning field does not read past the end", p["frame"] == EMPTY)

ok, p = parse(bytes([0x00]) + field(T_FILTER_SHOW, b"\x01\x02\x03\x04")
              + bytes([0xF0, 0x01]))
case("a good field before a bad one is kept", p["frame"] == ACCOUNT
     and p["filterLen"] == 4)

# --- refusals --------------------------------------------------------------
case("empty payload", parse(b"")[0] is False)
ok, p = parse(bytes([0x10, 0x00, 0x00, 0x00]))
case("version 1 is refused rather than guessed",
     ok is False and p["frame"] == NONE)
ok, p = parse(bytes([0xF0] + [0] * 10))
case("version 15 is refused", ok is False and p["frame"] == NONE)
ok, p = parse(bytes([0x10, 0x00, 0x00]))
case("but three bytes is a Model ID before any version check",
     p["frame"] == MODEL)

# --- unknown field types are skipped by length -----------------------------
ok, p = parse(bytes([0x00]) + field(0x9, b"\xDE\xAD\xBE\xEF")
              + field(T_FILTER_SHOW, b"\x01\x02\x03\x04"))
case("an unknown field is stepped over, not fatal",
     p["frame"] == ACCOUNT and p["filter"] == b"\x01\x02\x03\x04")

# --- identity: the property the parser must NOT promise --------------------
# Two units of the same model advertise the same Model ID. Anything that
# treats a Model ID as a device identity merges strangers' devices.
_, a = parse(bytes([0x2B, 0x71, 0xB2]))
_, c = parse(bytes([0x2B, 0x71, 0xB2]))
case("identical models are indistinguishable by Model ID",
     a["modelId"] == c["modelId"])
# The same device advertises a different filter after a salt rotation, so a
# filter is not an identity either.
_, d = parse(bytes([0x00]) + field(T_FILTER_SHOW, b"\x11\x22\x33\x44")
             + field(T_SALT, b"\x01"))
_, e = parse(bytes([0x00]) + field(T_FILTER_SHOW, b"\x9A\x0C\x5E\x71")
             + field(T_SALT, b"\x02"))
case("the same device under a new salt looks different",
     d["filter"] != e["filter"])

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
VALID = {NONE, MODEL, ACCOUNT, EMPTY}
for _ in range(80000):
    n = random.randint(0, 31)
    data = bytes(random.getrandbits(8) for _ in range(n))
    ok, p = parse(data)
    assert p["frame"] in VALID
    assert p["filterLen"] <= MAX_FILTER
    assert len(p["filter"]) == p["filterLen"]
    assert p["modelId"] <= 0xFFFFFF
    assert p["battery"]["count"] <= 3
    for v in p["battery"]["level"]:
        assert v <= 100 or v == LEVEL_UNKNOWN
    # a Model ID is only ever reported for a three-byte payload
    assert (p["frame"] == MODEL) == (n == 3)
    checks += 1

# every single-byte mutation of a real not-discoverable frame
base = bytearray(bytes([0x00]) + field(T_FILTER_SHOW, b"\x01\x02\x03\x04")
                 + field(T_SALT, b"\xC7")
                 + field(T_BATT_SHOW, bytes([0x40, 0xC8, 0x7F])))
for i in range(len(base)):
    for v in (0x00, 0x01, 0x7F, 0x80, 0xF0, 0xFF):
        m = bytearray(base)
        m[i] = v
        ok, p = parse(bytes(m))
        assert p["frame"] in VALID
        assert p["filterLen"] <= MAX_FILTER
        assert p["battery"]["count"] <= 3
        checks += 1

print("ok -- %d checks" % checks)
print("three bytes is a Model ID; every other length is not, and never "
      "reports one")
