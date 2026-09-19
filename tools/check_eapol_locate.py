"""Checks the EAPOL locator in ESP32-DIV/Eapol.cpp.

Finding the 802.1X payload in an 802.11 data frame means computing the
header length from the frame control field, and every field involved is
chosen by whoever transmitted the frame. The two ways to get it wrong are
reading past a short frame and knowing only some of the header layouts;
ESP32Marauder's version does both, which is why this one exists.

This transcribes the C line for line, behind a buffer that refuses any read
outside the frame, and checks three things: that the offset is right for
every header layout, that nothing outside a frame is ever read, and that the
frames which should be rejected are.

It says nothing about the compiled C. What it checks is that the arithmetic
the C encodes is right.

    python tools/check_eapol_locate.py
"""
TYPE_DATA = 0x02
SUBTYPE_QOS = 0x08
SUBTYPE_NULL = 0x04
TO_DS = 0x01
FROM_DS = 0x02
PROTECTED = 0x40
ORDER = 0x80
SNAP_LEN = 8


class Frame:
    """Bytes with a hard bound; any read outside [0, len) is a failure."""

    def __init__(self, data, sig_len):
        self.data = data
        self.len = sig_len
        self.max_read = -1

    def __getitem__(self, i):
        assert 0 <= i < self.len, "OUT OF BOUNDS READ at %d (len %d)" % (i, self.len)
        if i > self.max_read:
            self.max_read = i
        return self.data[i]


def header_length(f, length):
    if f is None or length < 2:
        return -1
    if ((f[0] >> 2) & 0x03) != TYPE_DATA:
        return -1
    subtype = (f[0] >> 4) & 0x0F
    if subtype & SUBTYPE_NULL:
        return -1
    flags = f[1]
    hdr = 24
    if (flags & (TO_DS | FROM_DS)) == (TO_DS | FROM_DS):
        hdr += 6
    if subtype & SUBTYPE_QOS:
        hdr += 2
        if flags & ORDER:
            hdr += 4
    return hdr


def find_payload(f, length):
    hdr = header_length(f, length)
    if hdr < 0:
        return -1
    if f[1] & PROTECTED:
        return -1
    if hdr + SNAP_LEN > length:
        return -1
    if f[hdr] != 0xAA or f[hdr + 1] != 0xAA or f[hdr + 2] != 0x03:
        return -1
    if f[hdr + 6] != 0x88 or f[hdr + 7] != 0x8E:
        return -1
    return hdr + SNAP_LEN


def locate(data, sig_len=None, want_frame=False):
    n = len(data) if sig_len is None else sig_len
    f = Frame(bytes(data), n)
    got = find_payload(f, n)
    return (got, f) if want_frame else got


def frame(qos=False, wds=False, order=False, protected=False, null=False,
          snap=True, ethertype=(0x88, 0x8E), body=16, mgmt=False):
    """Build a frame with the requested header shape."""
    subtype = 0
    if qos:
        subtype |= SUBTYPE_QOS
    if null:
        subtype |= SUBTYPE_NULL
    fc0 = (subtype << 4) | ((0x00 if mgmt else TYPE_DATA) << 2)
    fc1 = 0
    if wds:
        fc1 |= TO_DS | FROM_DS
    if order:
        fc1 |= ORDER
    if protected:
        fc1 |= PROTECTED

    hdr = 24 + (6 if wds else 0) + ((2 + (4 if order else 0)) if qos else 0)
    out = bytearray([fc0, fc1]) + bytearray(b"\x11" * (hdr - 2))
    if snap:
        out += bytes([0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00,
                      ethertype[0], ethertype[1]])
    else:
        out += bytes([0x00] * 8)
    out += b"\x22" * body
    return bytes(out), hdr


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


# --- every header layout finds the payload in the right place --------------
for qos in (False, True):
    for wds in (False, True):
        for order in (False, True):
            data, hdr = frame(qos=qos, wds=wds, order=order)
            expect = hdr + SNAP_LEN
            got = locate(data)
            case("layout qos=%d wds=%d order=%d -> %d" % (qos, wds, order, expect),
                 got == expect)

# the two layouts Marauder knows, spelled out
case("3-address non-QoS payload begins at 32", locate(frame()[0]) == 32)
case("3-address QoS payload begins at 34", locate(frame(qos=True)[0]) == 34)
# and the two it does not
case("4-address non-QoS payload begins at 38", locate(frame(wds=True)[0]) == 38)
case("QoS with HT Control begins at 38", locate(frame(qos=True, order=True)[0]) == 38)

# Order without QoS is not HT Control and must not shift anything
case("Order on a non-QoS frame does not move the payload",
     locate(frame(order=True)[0]) == 32)

# --- frames that must be rejected -----------------------------------------
case("management frames rejected", locate(frame(mgmt=True)[0]) == -1)
case("null data rejected", locate(frame(null=True)[0]) == -1)
case("QoS null rejected", locate(frame(qos=True, null=True)[0]) == -1)
case("protected frames rejected", locate(frame(protected=True)[0]) == -1)
case("non-SNAP payload rejected", locate(frame(snap=False)[0]) == -1)
case("SNAP with another ethertype rejected",
     locate(frame(ethertype=(0x08, 0x00))[0]) == -1)

# --- truncation ------------------------------------------------------------
full, hdr = frame()
for n in range(0, len(full) + 1):
    got = locate(full, n)
    if n >= hdr + SNAP_LEN:
        case("len %d still locates" % n, got == hdr + SNAP_LEN)
    else:
        case("len %d rejected rather than read past" % n, got == -1)

# The 33-byte case, which is the one the fixed-offset version gets wrong.
# A 3-address frame this long does contain a whole SNAP header, so 32 is the
# right answer and the locator gives it. The point is what was NOT read:
# Marauder tests payload[32] and payload[33], and at this length index 33 is
# off the end. The checked buffer above would have caught that.
got33, f33 = locate(full, 33, want_frame=True)
case("a 33-byte frame locates at 32", got33 == 32)
case("and the highest byte it read was 31, not 33", f33.max_read == 31)

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
for _ in range(80000):
    n = random.randint(0, 120)
    data = bytes(random.getrandbits(8) for _ in range(n))
    got = locate(data, n)
    assert got == -1 or (0 < got <= n), "nonsense offset %r for len %d" % (got, n)
    checks += 1

# frames that lie about being long, truncated at every length
for n in range(0, 64):
    data, _ = frame(qos=True, wds=True, order=True)
    locate(data, min(n, len(data)))
    checks += 1

print("ok -- %d checks, no out-of-bounds read" % checks)
print("payload offsets: 32 / 34 / 38 / 38 for 3-addr, QoS, 4-addr, QoS+HT")
