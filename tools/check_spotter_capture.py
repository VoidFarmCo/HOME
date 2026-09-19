"""Checks Spotter's capture ring, its deduplication, and its CSV escaping.

Capture writes a row per device to a file on the operator's SD card. Three
things about that are worth holding down, and none of them can be tested on
hardware that does not exist yet:

  the ring        frames arrive on the WiFi task and the card is written from
                  the main one. A full ring must drop rather than corrupt,
                  and must never hand out a record that was not written.

  the dedupe      one row per device, not per frame, or the file is hundreds
                  of rows a second. The table is direct-mapped, so a
                  collision costs a duplicate row and must not cost a device.

  the escaping    the SSID is arbitrary bytes off the air going into a text
                  file. A network named with a quote and a newline must not
                  be able to forge rows in somebody's capture.

    python tools/check_spotter_capture.py
"""
K_CAP_RING = 24
K_CAP_SEEN = 128
K_CAP_BATCH = 8
FNV_PRIME = 16777619
MASK32 = 0xFFFFFFFF


def fnv1a(h, b):
    return ((h ^ b) * FNV_PRIME) & MASK32


class Capture:
    def __init__(self):
        self.ring = [None] * K_CAP_RING
        self.head = 0
        self.tail = 0
        self.dropped = 0
        self.seen = [0] * K_CAP_SEEN
        self.logging = True

    def note(self, mac, rssi, fp, ssid):
        if not self.logging:
            return
        key = fp
        for b in mac:
            key = fnv1a(key, b)
        if key == 0:
            key = 1
        slot = key % K_CAP_SEEN
        if self.seen[slot] == key:
            return                       # already written
        self.seen[slot] = key
        nxt = (self.head + 1) % K_CAP_RING
        if nxt == self.tail:
            self.dropped += 1
            return
        self.ring[self.head] = dict(mac=bytes(mac), rssi=rssi, fp=fp,
                                    ssid=ssid[:32])
        self.head = nxt

    def flush(self):
        out = []
        while self.tail != self.head and len(out) < K_CAP_BATCH:
            rec = self.ring[self.tail]
            assert rec is not None, "flushed a slot that was never written"
            out.append(rec)
            self.tail = (self.tail + 1) % K_CAP_RING
        return out


def csv_row(rec):
    """The formatting in captureFlush(), including the escaping."""
    mac = ":".join("%02X" % b for b in rec["mac"])
    head = "%d,%s,%d,%08X,%d,%d," % (0, mac, 1 if rec["mac"][0] & 0x02 else 0,
                                     rec["fp"], rec["rssi"], 6)
    body = ""
    for ch in rec["ssid"]:
        c = ord(ch) if isinstance(ch, str) else ch
        body += chr(c) if (32 <= c < 127 and c != ord('"')) else "."
    return head + '"' + body + '"'


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


MAC_A = bytes([0x02, 0x11, 0x22, 0x33, 0x44, 0x01])
MAC_B = bytes([0xB4, 0x1E, 0x52, 0x00, 0x00, 0x02])
FP_A = 0x25567F65
FP_B = 0x1234ABCD

# --- dedupe ---------------------------------------------------------------
c = Capture()
for _ in range(500):
    c.note(MAC_A, -50, FP_A, "Flock-1A2B3C")
case("500 frames from one device -> one row", len(c.flush()) == 1)

c = Capture()
c.note(MAC_A, -50, FP_A, "x")
c.note(MAC_A, -50, FP_B, "x")
case("same address, new fingerprint -> a second row", len(c.flush()) == 2)

c = Capture()
c.note(MAC_A, -50, FP_A, "x")
c.note(MAC_B, -50, FP_A, "x")
case("same fingerprint, new address -> a second row", len(c.flush()) == 2)

# --- the ring -------------------------------------------------------------
c = Capture()
for i in range(200):
    c.note(bytes([0x02, 0, 0, 0, i >> 8, i & 0xFF]), -50, FP_A ^ i, "s")
case("a full ring drops rather than overwriting", c.dropped > 0)
first = c.flush()
case("and still yields a full batch of real records", len(first) == K_CAP_BATCH)
case("ring never hands out an unwritten slot", all(r is not None for r in first))

c = Capture()
total = 0
for i in range(1000):
    c.note(bytes([0x02, 0, 0, 0, i >> 8, i & 0xFF]), -50, FP_A ^ i, "s")
    total += len(c.flush())             # drained every frame, as the loop does
case("drained as it goes, nothing is dropped", c.dropped == 0)
case("and every distinct device produced a row", total == 1000)

# wrap: the ring index must survive many laps
c = Capture()
seen_rows = 0
for lap in range(100):
    for i in range(K_CAP_RING - 1):
        n = lap * 1000 + i
        c.note(bytes([0x02, 0, 0, n >> 16 & 0xFF, n >> 8 & 0xFF, n & 0xFF]),
               -50, FP_A ^ n, "s")
    while True:
        b = c.flush()
        if not b:
            break
        seen_rows += len(b)
case("100 laps of the ring stay consistent", c.dropped == 0 and seen_rows == 2300)

# --- escaping -------------------------------------------------------------
def escaped(ssid):
    return csv_row(dict(mac=MAC_A, rssi=-50, fp=FP_A, ssid=ssid))


row = escaped('ev"il')
case("a quote in an SSID cannot close the field", row.count('"') == 2)

row = escaped("a\nb\rc")
case("a newline in an SSID cannot start a new row",
     "\n" not in row and "\r" not in row)

row = escaped("a,b,c")
case("a comma stays inside the quoted field", row.count(",") == 6 + 2)

row = escaped("".join(chr(x) for x in range(0, 32)))
case("control characters are replaced", all(ord(ch) >= 32 for ch in row))

row = escaped('","," ')
case("a crafted SSID cannot forge columns", row.count('"') == 2)

# truncation happens in captureNote, not in the formatter, so go through it
c = Capture()
c.note(MAC_A, -50, FP_A, "A" * 200)
rec = c.flush()[0]
case("a long SSID is truncated where it is captured", len(rec["ssid"]) == 32)
case("and the row it produces is still one quoted field",
     csv_row(rec).count('"') == 2)

print("ok -- %d capture rules hold" % checks)
