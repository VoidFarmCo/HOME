"""Checks the BLE advertisement builders in ESP32-DIV/bluetooth.cpp.

An advertisement is a chain of length-prefixed AD structures. Get one length
byte wrong and the packet is still transmitted, still looks fine in the log,
and is silently dropped or misparsed by every receiver -- there is no error
anywhere to notice. That is the failure this file exists to catch, so the
central check walks each packet the way a receiver does and insists the walk
lands exactly on the end.

Covers the two templates added for Swift Pair and Flipper Zero. The older
Apple, Samsung and Google templates are fixed byte arrays copied verbatim
rather than assembled at runtime, so there are no lengths to get wrong; they
are walked here anyway, because the walk is free and the arrays have never
been checked.

    python tools/check_ble_adv.py
"""
MAX_ADV = 31

# ── Swift Pair, from Microsoft's component guidelines ──────────────────────
SWIFT_BEACON_ID = 0x03
SWIFT_SUB_LE_ONLY = 0x00
SWIFT_RESERVED_RSSI = 0x80
MS_VENDOR_ID = 0x0006

FLIPPER_SERVICE_BASE = 0x3080

SWIFT_ALPHA = "ABCDEFGHJKLMNPQRSTUVWXYZ23456789"
FLIPPER_ALPHA = "abcdefghijklmnopqrstuvwxyz"


def build_swift_pair(picks):
    """generateSwiftPairAdvPacket(), transcribed. `picks` stands in for
    random() so the output is reproducible."""
    name = "Pueo-" + "".join(SWIFT_ALPHA[p] for p in picks)
    name_len = len(name)
    raw = bytearray()
    raw += bytes([0x02, 0x01, 0x06])
    raw.append(1 + 2 + 1 + 1 + 1 + name_len)
    raw.append(0xFF)
    raw.append(0x06)
    raw.append(0x00)
    raw.append(SWIFT_BEACON_ID)
    raw.append(SWIFT_SUB_LE_ONLY)
    raw.append(SWIFT_RESERVED_RSSI)
    raw += name.encode("ascii")
    return bytes(raw)


def build_flipper(picks, colour):
    """generateFlipperAdvPacket(), transcribed."""
    name = "Flipper " + "".join(FLIPPER_ALPHA[p] for p in picks)
    name_len = len(name)
    uuid = FLIPPER_SERVICE_BASE | colour
    raw = bytearray()
    raw += bytes([0x02, 0x01, 0x06])
    raw.append(1 + name_len)
    raw.append(0x09)
    raw += name.encode("ascii")
    raw += bytes([0x03, 0x02, uuid & 0xFF, uuid >> 8])
    raw += bytes([0x02, 0x0A, 0x00])
    return bytes(raw)


def walk(packet):
    """Parse a packet as a receiver does. Returns the list of
    (type, body) structures, or raises on a malformed length."""
    out = []
    i = 0
    n = len(packet)
    while i < n:
        length = packet[i]
        if length == 0:
            break                       # early terminator: the rest is padding
        if i + 1 + length > n:
            raise ValueError("AD structure at %d overruns the packet" % i)
        out.append((packet[i + 1], packet[i + 2:i + 1 + length]))
        i += 1 + length
    if i != n:
        raise ValueError("walk ended at %d, packet is %d" % (i, n))
    return out


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


# ── Swift Pair ─────────────────────────────────────────────────────────────
p = build_swift_pair([0, 1, 2, 3])
structs = walk(p)
case("swift pair: walk consumes the whole packet", True)
case("swift pair: two AD structures", len(structs) == 2)
case("swift pair: flags first", structs[0][0] == 0x01
     and structs[0][1] == b"\x06")
case("swift pair: vendor-specific second", structs[1][0] == 0xFF)

body = structs[1][1]
case("swift pair: Microsoft vendor ID, little endian",
     body[0] | (body[1] << 8) == MS_VENDOR_ID)
case("swift pair: beacon ID", body[2] == SWIFT_BEACON_ID)
case("swift pair: sub scenario is LE-only", body[3] == SWIFT_SUB_LE_ONLY)
case("swift pair: sub scenario is one the spec defines",
     body[3] in (0x00, 0x01, 0x02))
case("swift pair: reserved RSSI byte", body[4] == SWIFT_RESERVED_RSSI)
case("swift pair: the rest is the display name",
     body[5:] == b"Pueo-ABCD")
case("swift pair: fits in an advertisement", len(p) <= MAX_ADV)

# the name varies per burst, which is what makes repeat notifications appear
seen = set()
for a in range(len(SWIFT_ALPHA)):
    for b in range(len(SWIFT_ALPHA)):
        q = build_swift_pair([a, b, 0, 0])
        walk(q)
        case("swift pair %d/%d walks" % (a, b), len(q) <= MAX_ADV)
        seen.add(bytes(walk(q)[1][1][5:]))
case("swift pair: names actually differ", len(seen) == len(SWIFT_ALPHA) ** 2)

# ── Flipper Zero ───────────────────────────────────────────────────────────
p = build_flipper([0, 1, 2, 3, 4, 5], 2)
structs = walk(p)
case("flipper: four AD structures", len(structs) == 4)
case("flipper: flags", structs[0][0] == 0x01)
case("flipper: complete local name", structs[1][0] == 0x09)
case("flipper: the name looks like a Flipper's",
     structs[1][1] == b"Flipper abcdef")
case("flipper: incomplete 16-bit UUID list", structs[2][0] == 0x02)
case("flipper: UUID is little endian on the wire",
     structs[2][1] == bytes([0x82, 0x30]))
case("flipper: which is 0x3082", (structs[2][1][0] | (structs[2][1][1] << 8))
     == 0x3082)
case("flipper: TX power", structs[3][0] == 0x0A)
case("flipper: fits in an advertisement", len(p) <= MAX_ADV)

for colour in (1, 2, 3):
    q = build_flipper([0] * 6, colour)
    s = walk(q)
    uuid = s[2][1][0] | (s[2][1][1] << 8)
    case("flipper colour %d gives 0x%04X" % (colour, uuid),
         uuid == FLIPPER_SERVICE_BASE | colour)
    case("flipper colour %d is in the 0x308x family" % colour,
         0x3081 <= uuid <= 0x3083)

# colour 0 is the uncoloured value and is never sent
case("flipper never sends the base UUID bare",
     all(walk(build_flipper([0] * 6, c))[2][1][0] != 0x80 for c in (1, 2, 3)))

# every name the builder can produce still fits
for a in range(len(FLIPPER_ALPHA)):
    q = build_flipper([a] * 6, 3)
    walk(q)
    case("flipper name %d fits" % a, len(q) <= MAX_ADV)

# ── the existing fixed templates, walked for the first time ────────────────
SAMSUNG = bytes([14, 0xFF, 0x75, 0x00, 0x01, 0x00, 0x02, 0x00, 0x01, 0x01,
                 0xFF, 0x00, 0x00, 0x43, 0x00])
GOOGLE = bytes([0x03, 0x03, 0x2C, 0xFE,
                0x06, 0x16, 0x2C, 0xFE, 0x00, 0xB7, 0x27,
                0x02, 0x0A, 0x00])
APPLE = bytes([0x1e, 0xff, 0x4c, 0x00, 0x07, 0x19, 0x07, 0x02, 0x20, 0x75,
               0xaa, 0x30, 0x01, 0x00, 0x00, 0x45, 0x12, 0x12, 0x12, 0x00,
               0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
               0x00])

s = walk(SAMSUNG)
case("samsung template walks", len(s) == 1 and s[0][0] == 0xFF)
case("samsung company ID 0x0075",
     s[0][1][0] | (s[0][1][1] << 8) == 0x0075)
case("samsung fits", len(SAMSUNG) <= MAX_ADV)

s = walk(GOOGLE)
case("google template walks", len(s) == 3)
case("google: complete 16-bit service UUIDs", s[0][0] == 0x03
     and s[0][1] == bytes([0x2C, 0xFE]))
case("google: service data under 0xFE2C", s[1][0] == 0x16
     and s[1][1][:2] == bytes([0x2C, 0xFE]))
# three bytes of service data is a Fast Pair Model ID -- see FastPair.cpp
fp = s[1][1][2:]
case("google: three bytes of Fast Pair service data", len(fp) == 3)
case("google: which is model ID 00B727",
     (fp[0] << 16 | fp[1] << 8 | fp[2]) == 0x00B727)
case("google: TX power last", s[2][0] == 0x0A)
case("google fits", len(GOOGLE) <= MAX_ADV)

s = walk(APPLE)
case("apple template walks", len(s) == 1 and s[0][0] == 0xFF)
case("apple company ID 0x004C",
     s[0][1][0] | (s[0][1][1] << 8) == 0x004C)
case("apple: continuity type 0x07, proximity pairing", s[0][1][2] == 0x07)
case("apple fits exactly", len(APPLE) == MAX_ADV)

# ── the walk itself must actually reject bad lengths ───────────────────────
for bad in (bytes([0x05, 0x01, 0x06]),          # claims more than it has
            bytes([0x02, 0x01, 0x06, 0x09]),    # trailing byte, no length
            bytes([0xFF, 0x01])):
    try:
        walk(bad)
        raise AssertionError("walk accepted %r" % bad)
    except ValueError:
        checks += 1

print("ok -- %d checks" % checks)
print("every packet's AD walk lands exactly on its end")
