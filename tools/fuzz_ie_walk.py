"""Bounds check for the information-element walk in Spotter.cpp.

onPacket() parses frames off the air, so every length in them is attacker
controlled. This transcribes the walk line for line into Python, behind a
buffer that refuses any read outside the frame, and throws malformed frames
at it: elements claiming more than the frame holds, elements sitting exactly
on the last byte, runs of zero-length elements, and sixty thousand random
ones.

Since step 2 the walk also reads element *contents* to build the
fingerprint, which is a good deal more reach than reading id and length
alone -- a vendor element reads four bytes in, a capabilities element reads
all of them. Those reads go through the same checked buffer here.

It proves nothing about the compiled C. What it checks is that the bounds
reasoning the C encodes is right, which is the part that is easy to get
wrong and expensive to get wrong. Re-run it whenever the walk changes.

    python tools/fuzz_ie_walk.py
"""
import random

K_MAX_IES = 32
FNV_BASIS = 2166136261
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF

WITH_CONTENTS = (0x01, 0x2D, 0x32, 0x7F, 0xBF)


def fnv1a(h, b):
    return ((h ^ b) * FNV_PRIME) & MASK32


class Frame:
    """Bytes with a hard bound. Any read outside [0, len) is a failure, which
    on the device would be a read of whatever sits after the packet buffer."""

    def __init__(self, data, sig_len):
        self.data = data
        self.len = sig_len
        self.reads = []

    def __getitem__(self, i):
        assert 0 <= i < self.len, "OUT OF BOUNDS READ at %d (len %d)" % (i, self.len)
        self.reads.append(i)
        return self.data[i]


def walk(p, length, ie_start):
    ssid_val = None
    ssid_len = 0
    fp = FNV_BASIS
    hashed = 0
    off = ie_start
    iterations = 0

    for _seen in range(K_MAX_IES):
        iterations += 1
        if off + 2 > length:
            break
        ie_id = p[off]
        ilen = p[off + 1]
        if off + 2 + ilen > length:
            break
        val = off + 2

        if ie_id == 0x00 and ssid_val is None:
            ssid_val = val
            ssid_len = ilen

        fp = fnv1a(fp, ie_id)
        hashed += 1

        if ie_id in (0x00, 0x03):
            pass
        elif ie_id == 0xDD:
            for k in range(min(ilen, 4)):
                fp = fnv1a(fp, p[val + k])
        elif ie_id == 0xFF:
            if ilen >= 1:
                fp = fnv1a(fp, p[val])
        elif ie_id in WITH_CONTENTS:
            for k in range(ilen):
                fp = fnv1a(fp, p[val + k])

        off += 2 + ilen

    if hashed == 0:
        fp = 0
    elif fp == 0:
        fp = 1

    ssid = None
    if ssid_val is not None and 0 < ssid_len <= 32:
        for i in range(ssid_val, ssid_val + ssid_len):
            p[i]                      # memcpy(ssid, ssidVal, ssidLen)
        ssid = (ssid_val, ssid_len)
    return ssid, iterations, off, fp


def run(data, sig_len, ie_start):
    f = Frame(data, sig_len)
    ssid, iters, _off, fp = walk(f, sig_len, ie_start)
    assert iters <= K_MAX_IES, "loop ran %d times" % iters
    assert 0 <= fp <= MASK32
    return ssid, fp


random.seed(20260919)
cases = 0

# 1. pure random frames, random truncation
for _ in range(60000):
    n = random.randint(0, 400)
    data = bytes(random.getrandbits(8) for _ in range(n))
    sig_len = random.randint(24, max(24, n)) if n >= 24 else 24
    data = data + b"\x00" * max(0, sig_len - len(data))
    run(data, sig_len, random.choice([24, 36]))
    cases += 1

# 2. an element that claims far more than the frame holds, for every id that
#    reads contents
for ie_start in (24, 36):
    for ie_id in (0x00, 0x01, 0x03, 0x2D, 0x7F, 0xBF, 0xDD, 0xFF):
        for sig_len in range(24, 80):
            for claim in (0, 1, 3, 4, 254, 255):
                data = bytearray(b"\x11" * (sig_len + 8))
                if sig_len > ie_start + 1:
                    data[ie_start] = ie_id
                    data[ie_start + 1] = claim
                run(bytes(data), sig_len, ie_start)
                cases += 1

# 3. a contents-bearing element ending exactly on the last byte of the frame
for ie_id in (0x01, 0x2D, 0xDD, 0xFF):
    for ilen in range(0, 40):
        ie_start = 24
        sig_len = ie_start + 2 + ilen
        data = bytearray(b"\x00" * sig_len)
        data[ie_start] = ie_id
        data[ie_start + 1] = ilen
        run(bytes(data), sig_len, ie_start)
        cases += 1

# 4. many zero-length elements: does the walk terminate, and does the cap hold
data = bytearray(b"\x00" * 24 + b"\x01\x00" * 200)
run(bytes(data), len(data), 24)
cases += 1

# 5. the wildcard probe -- SSID present, length zero, elements after it.
#    Before step 1 this frame was discarded at the SSID; it must now be
#    walked to the end and produce a fingerprint.
wildcard = bytearray(b"\x40" + b"\x00" * 23)
wildcard += bytes([0x00, 0x00])                            # SSID, zero length
wildcard += bytes([0x01, 0x04, 0x02, 0x04, 0x0b, 0x16])    # supported rates
wildcard += bytes([0x32, 0x02, 0x30, 0x48])                # extended rates
wildcard += bytes([0x2d, 0x02, 0x21, 0x00])                # HT capabilities
f = Frame(bytes(wildcard), len(wildcard))
ssid, iters, end_off, fp_wild = walk(f, len(wildcard), 24)
assert ssid is None, "wildcard must not produce an SSID match"
assert end_off == len(wildcard), "walk stopped early: %d of %d" % (end_off, len(wildcard))
assert iters == 5
assert fp_wild != 0, "wildcard probe must still yield a fingerprint"
cases += 1

# 6. the point of the whole exercise: the SSID and the channel must not move
#    the fingerprint, and anything else must.
def probe(ssid_bytes, channel, rates=(0x02, 0x04, 0x0b, 0x16)):
    fr = bytearray(b"\x40" + b"\x00" * 23)
    fr += bytes([0x00, len(ssid_bytes)]) + ssid_bytes
    fr += bytes([0x01, len(rates)]) + bytes(rates)
    fr += bytes([0x03, 0x01, channel])                     # DS Parameter Set
    fr += bytes([0xdd, 0x05, 0x00, 0x50, 0xf2, 0x08, 0x99])  # vendor specific
    return walk(Frame(bytes(fr), len(fr)), len(fr), 24)[3]

base = probe(b"HomeNet", 1)
assert probe(b"OtherNetwork", 1) == base, "SSID changed the fingerprint"
assert probe(b"", 1) == base, "wildcard vs directed changed the fingerprint"
assert probe(b"HomeNet", 11) == base, "channel changed the fingerprint"
assert probe(b"HomeNet", 1, rates=(0x02, 0x04)) != base, "rates did not change it"
cases += 4

# vendor element payload past the OUI and type must not matter
def vendor(tail):
    fr = bytearray(b"\x40" + b"\x00" * 23)
    fr += bytes([0x00, 0x00])
    fr += bytes([0xdd, 4 + len(tail), 0x00, 0x50, 0xf2, 0x08]) + bytes(tail)
    return walk(Frame(bytes(fr), len(fr)), len(fr), 24)[3]

assert vendor(b"\x01\x02") == vendor(b"\xfe\xfd"), "vendor payload changed the fingerprint"
cases += 1

print("ok -- %d frames, no out-of-bounds read, loop always terminated" % cases)
print("fingerprint is stable across SSID and channel, and moves with the")
print("capability contents. Wildcard probes yield one: %08X" % fp_wild)
