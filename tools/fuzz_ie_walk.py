"""Bounds check for the information-element walk in Spotter.cpp.

onPacket() parses frames off the air, so every length in them is attacker
controlled. This transcribes the walk line for line into Python, behind a
buffer that refuses any read outside the frame, and throws malformed frames
at it: elements claiming more than the frame holds, elements sitting exactly
on the last byte, runs of zero-length elements, and sixty thousand random
ones.

It proves nothing about the compiled C. What it checks is that the bounds
reasoning the C encodes is right, which is the part that is easy to get
wrong and expensive to get wrong. Re-run it whenever the walk changes --
step 2 of docs/pueo/ie-fingerprinting.md will add contents reads to it.

    python tools/fuzz_ie_walk.py
"""
import random

K_MAX_IES = 32


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
    """for (int seen = 0; seen < kMaxIes; seen++) { ... }"""
    ssid_val = None
    ssid_len = 0
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
        if ie_id == 0x00 and ssid_val is None:
            ssid_val = off + 2
            ssid_len = ilen
        off += 2 + ilen
    # the copy that follows the walk
    if ssid_val is None or ssid_len == 0 or ssid_len > 32:
        return None, iterations, off
    for i in range(ssid_val, ssid_val + ssid_len):
        p[i]                      # memcpy(ssid, ssidVal, ssidLen)
    return (ssid_val, ssid_len), iterations, off


def run(data, sig_len, ie_start):
    f = Frame(data, sig_len)
    res, iters, _off = walk(f, sig_len, ie_start)
    assert iters <= K_MAX_IES, "loop ran %d times" % iters
    return res


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

# 2. adversarial: an element that claims far more than the frame holds
for ie_start in (24, 36):
    for sig_len in range(24, 80):
        for claim in (0, 1, 254, 255):
            data = bytearray(b"\x11" * (sig_len + 8))
            if sig_len > ie_start + 1:
                data[ie_start] = 0x00
                data[ie_start + 1] = claim
            run(bytes(data), sig_len, ie_start)
            cases += 1

# 3. an SSID element sitting exactly at the end of the frame
for ssid_len in range(0, 40):
    ie_start = 24
    sig_len = ie_start + 2 + ssid_len
    data = bytearray(b"\x00" * sig_len)
    data[ie_start] = 0x00
    data[ie_start + 1] = ssid_len
    run(bytes(data), sig_len, ie_start)
    cases += 1

# 4. many zero-length elements: does the walk terminate, and does the cap hold
data = bytearray(b"\x00" * 24 + b"\x01\x00" * 200)
run(bytes(data), len(data), 24)
cases += 1

# 5. the wildcard probe -- SSID present, length zero, elements after it.
#    The old code returned here; the walk must still traverse the rest.
frame = bytearray(b"\x40" + b"\x00" * 23)     # header
frame += bytes([0x00, 0x00])                  # SSID, zero length
frame += bytes([0x01, 0x04, 0x02, 0x04, 0x0b, 0x16])   # supported rates
frame += bytes([0x32, 0x02, 0x30, 0x48])      # extended rates
frame += bytes([0x2d, 0x02, 0x21, 0x00])      # HT caps
f = Frame(bytes(frame), len(frame))
res, iters, end_off = walk(f, len(frame), 24)
assert res is None, "wildcard must not produce an SSID match"
assert end_off == len(frame), "walk stopped early: reached %d of %d" % (
    end_off, len(frame))
assert iters == 5, "expected 4 elements then the bound check, got %d" % iters
cases += 1

print("ok -- %d frames, no out-of-bounds read, loop always terminated" % cases)
print("wildcard probe: walked all 4 elements to offset %d of %d, no SSID match"
      % (end_off, len(frame)))
