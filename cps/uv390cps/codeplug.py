"""Editable model of the MDUV380 OpenGD77 codeplug (layout from application/source/functions/codeplug.c).

On the MDUV380 the 'EEPROM' lives at raw SPI flash address 0, the second part ('flash') at
FLASH_OFFSET (128 KiB) + the OpenGD77 addresses.  Every record keeps its raw bytes, so fields this
program does not know about are written back unchanged.
"""
import copy
import json
import struct

FLASH_OFFSET = 0x20000

# (raw address, length) of everything we read
REGIONS = [
    (0x0000, 0xC000),                                   # general settings, DTMF, APRS, zones, first 128 channels
    (FLASH_OFFSET + 0x7B1B0, 7 * (128 * 56 + 16)),      # channels 129..1024
    (FLASH_OFFSET + 0x87620, 1024 * 24),                # contacts
    (FLASH_OFFSET + 0x8D620, 0x80 + 76 * 80),           # TG list lengths + TG lists
]

CH_SIZE = 56
ADDR_CH_HDR_EE, ADDR_CH_EE = 0x3780, 0x3790
ADDR_CH_HDR_FL, ADDR_CH_FL = FLASH_OFFSET + 0x7B1B0, FLASH_OFFSET + 0x7B1C0
ADDR_CONTACTS = FLASH_OFFSET + 0x87620
ADDR_RXGROUP_LEN = FLASH_OFFSET + 0x8D620
ADDR_RXGROUP = FLASH_OFFSET + 0x8D6A0
ADDR_ZONE_INUSE, ADDR_ZONE_LIST = 0x8010, 0x8030
ADDR_GENERAL = 0xE0

MAX_CHANNELS, MAX_CONTACTS, MAX_TGLISTS, MAX_ZONES = 1024, 1024, 76, 68
CALL_TYPES = ["Group", "Private", "All"]

CTCSS = ["67.0", "69.3", "71.9", "74.4", "77.0", "79.7", "82.5", "85.4", "88.5", "91.5", "94.8", "97.4", "100.0",
         "103.5", "107.2", "110.9", "114.8", "118.8", "123.0", "127.3", "131.8", "136.5", "141.3", "146.2", "151.4",
         "156.7", "159.8", "162.2", "165.5", "167.9", "171.3", "173.8", "177.3", "179.9", "183.5", "186.2", "189.9",
         "192.8", "196.6", "199.5", "203.5", "206.5", "210.7", "218.1", "225.7", "229.1", "233.6", "241.8", "250.3",
         "254.1"]


class Image:
    """Sparse copy of the radio memory (what we read), can be saved to / loaded from a file."""

    def __init__(self, regions=None):
        self.regions = regions or {}      # addr -> bytearray

    def copy(self):
        return Image({a: bytearray(d) for a, d in self.regions.items()})

    def _find(self, addr, length):
        for start, data in self.regions.items():
            if start <= addr and addr + length <= start + len(data):
                return start, data
        raise KeyError("0x%X not in image" % addr)

    def get(self, addr, length):
        start, data = self._find(addr, length)
        return bytes(data[addr - start:addr - start + length])

    def set(self, addr, value):
        start, data = self._find(addr, len(value))
        data[addr - start:addr - start + len(value)] = value

    def save(self, path):
        with open(path, "w") as f:
            json.dump({"format": "uv390cps-image-1", "regions": {"%d" % a: bytes(d).hex() for a, d in self.regions.items()}}, f)

    @staticmethod
    def load(path):
        with open(path) as f:
            j = json.load(f)
        if j.get("format") != "uv390cps-image-1":
            raise ValueError("not a uv390cps image")
        return Image({int(a): bytearray.fromhex(d) for a, d in j["regions"].items()})

    def diff(self, other):
        """Changed byte spans (addr, bytes) of `other` compared to self, small gaps merged."""
        spans = []
        for addr, a in self.regions.items():
            b = other.regions[addr]
            i, n = 0, len(a)
            while i < n:
                if a[i] == b[i]:
                    i += 1
                    continue
                j = i
                last = i
                while j < n and j - last <= 16:
                    if a[j] != b[j]:
                        last = j
                    j += 1
                spans.append((addr + i, bytes(b[i:last + 1])))
                i = last + 1
        return spans


def read_image(radio, progress=None):
    img = Image()
    total = sum(l for _, l in REGIONS)
    done = 0
    for addr, length in REGIONS:
        base = done
        img.regions[addr] = bytearray(radio.read(1, addr, length,
                                                 (lambda n, t, b=base: progress(b + n, total)) if progress else None))
        done += length
    return img


# ---------------------------------------------------------------- helpers
def _name(b):
    b = b.split(b"\xff")[0].split(b"\0")[0]
    return b.decode("latin-1").rstrip()


def _pack_name(s, n=16):
    return s.encode("latin-1", "replace")[:n].ljust(n, b"\xff")


def bcd2int(v):
    r, m = 0, 1
    while v:
        r += (v & 0xF) * m
        m *= 10
        v >>= 4
    return r


def int2bcd(i):
    r, shift = 0, 0
    while i:
        r += (i % 10) << shift
        i //= 10
        shift += 4
    return r


def tone_to_str(t):
    if t in (0, 0xFFFF):
        return ""
    if t & 0xC000:
        return "D%03X%s" % (t & 0x3FFF, "I" if t & 0x4000 else "N")
    return "%.1f" % (bcd2int(t) / 10)


def tone_from_str(s):
    s = (s or "").strip().upper()
    if not s or s == "NONE":
        return 0
    if s[0] == "D":
        inv = s.endswith("I")
        code = int(s[1:4], 16)
        return (0xC000 if inv else 0x8000) | code
    return int2bcd(int(round(float(s) * 10)))


def _bit(buf, i):
    return (buf[i // 8] >> (i % 8)) & 1


def _setbit(buf, i, v):
    if v:
        buf[i // 8] |= 1 << (i % 8)
    else:
        buf[i // 8] &= ~(1 << (i % 8)) & 0xFF


def _channel_location(n):
    """-> (header address, bit index, record address) of channel n (1 based)."""
    bank, i = divmod(n - 1, 128)
    if bank == 0:
        return ADDR_CH_HDR_EE, i, ADDR_CH_EE + i * CH_SIZE
    h = ADDR_CH_HDR_FL + (bank - 1) * (128 * CH_SIZE + 16)
    return h, i, h + 16 + i * CH_SIZE


def _snap(rec):
    rec["_orig"] = {k: copy.copy(v) for k, v in rec.items() if k not in ("raw", "_orig")}
    return rec


def _chg(rec, key):
    """True if `key` was edited (new records have no snapshot, so everything counts as edited)."""
    return rec.get("_orig", {}).get(key, object()) != rec[key]


# ---------------------------------------------------------------- model
class Codeplug:
    def __init__(self, img):
        self.img = img
        self.channels = {}      # number -> dict
        self.contacts = {}
        self.tglists = {}
        self.zones = []         # ordered; each has "slot"
        self.general = {}
        self._parse()
        self.orig_keys = {"channels": set(self.channels), "contacts": set(self.contacts),
                          "tglists": set(self.tglists), "zones": {z["slot"] for z in self.zones}}

    # ---- parsing
    def _parse(self):
        img = self.img
        for n in range(1, MAX_CHANNELS + 1):
            hdr, bit, addr = _channel_location(n)
            if not _bit(img.get(hdr, 16), bit):
                continue
            d = img.get(addr, CH_SIZE)
            f2, f4 = d[49], d[51]
            digital = d[24] != 0
            self.channels[n] = _snap({
                "number": n, "raw": d, "name": _name(d[:16]), "mode": "DMR" if digital else "FM",
                "rx": bcd2int(struct.unpack_from("<I", d, 16)[0]) / 100000,
                "tx": bcd2int(struct.unpack_from("<I", d, 20)[0]) / 100000,
                "power": "High" if f4 & 0x80 else "Low",
                "bw": "25" if f4 & 0x02 else "12.5",
                "rxtone": tone_to_str(struct.unpack_from("<H", d, 32)[0]),
                "txtone": tone_to_str(struct.unpack_from("<H", d, 34)[0]),
                "colour": d[44], "slot": 2 if f2 & 0x40 else 1,
                "contact": struct.unpack_from("<H", d, 46)[0], "tglist": d[43],
                "rxonly": bool(f4 & 0x04), "zoneskip": bool(f4 & 0x20), "allskip": bool(f4 & 0x10),
            })
        for i in range(MAX_CONTACTS):
            d = img.get(ADDR_CONTACTS + i * 24, 24)
            if d[0] == 0xFF:
                continue
            h = d[16:20].hex()
            self.contacts[i + 1] = _snap({"number": i + 1, "raw": d, "name": _name(d[:16]),
                                          "id": int(h) if h.isdigit() else 0,
                                          "type": CALL_TYPES[d[20]] if d[20] < 3 else "Group"})
        lens = img.get(ADDR_RXGROUP_LEN, MAX_TGLISTS)
        for i in range(MAX_TGLISTS):
            d = img.get(ADDR_RXGROUP + i * 80, 80)
            if d[0] in (0xFF, 0):
                continue
            ids = [c for c in struct.unpack_from("<32H", d, 16) if c]
            self.tglists[i + 1] = _snap({"number": i + 1, "raw": d, "name": _name(d[:16]), "contacts": ids,
                                         "inactive": lens[i] == 0})
        self.per_zone = 80 if img.get(0x8060, 16)[15] <= 4 else 16
        size = 16 + 2 * self.per_zone
        inuse = img.get(ADDR_ZONE_INUSE, 32)
        for slot in range(256):
            if not _bit(inuse, slot):
                continue
            d = img.get(ADDR_ZONE_LIST + slot * size, size)
            self.zones.append(_snap({"slot": slot, "raw": d, "name": _name(d[:16]),
                                     "channels": [c for c in struct.unpack_from("<%dH" % self.per_zone, d, 16) if c]}))
        g = img.get(ADDR_GENERAL, 16)
        h = g[8:12].hex()
        self.general = _snap({"callsign": _name(g[:8]), "dmrid": int(h) if h.isdigit() else 0, "raw": g})

    # ---- editing helpers
    def free_number(self, table, limit):
        for n in range(1, limit + 1):
            if n not in table:
                return n
        return None

    def new_channel(self):
        n = self.free_number(self.channels, MAX_CHANNELS)
        if n is None:
            return None
        raw = bytearray(CH_SIZE)
        raw[:16] = b"\xff" * 16
        self.channels[n] = {"number": n, "raw": bytes(raw), "name": "New channel", "mode": "FM", "rx": 145.5,
                            "tx": 145.5, "power": "Low", "bw": "12.5", "rxtone": "", "txtone": "", "colour": 1,
                            "slot": 1, "contact": 0, "tglist": 0, "rxonly": False, "zoneskip": False,
                            "allskip": False}
        return n

    def delete_channel(self, n):
        self.channels.pop(n, None)
        for z in self.zones:
            z["channels"] = [c for c in z["channels"] if c != n]

    def new_contact(self):
        n = self.free_number(self.contacts, MAX_CONTACTS)
        if n is None:
            return None
        raw = bytearray(b"\xff" * 24)
        raw[20:24] = b"\x00\x00\x00\x00"
        self.contacts[n] = {"number": n, "raw": bytes(raw), "name": "New contact", "id": 1, "type": "Group"}
        return n

    def delete_contact(self, n):
        self.contacts.pop(n, None)
        for c in self.channels.values():
            if c["contact"] == n:
                c["contact"] = 0
        for t in self.tglists.values():
            t["contacts"] = [c for c in t["contacts"] if c != n]

    def new_tglist(self):
        n = self.free_number(self.tglists, MAX_TGLISTS)
        if n is None:
            return None
        self.tglists[n] = {"number": n, "raw": b"\0" * 80, "name": "New list", "contacts": [], "inactive": True}
        return n

    def delete_tglist(self, n):
        self.tglists.pop(n, None)
        for c in self.channels.values():
            if c["tglist"] == n:
                c["tglist"] = 0

    def new_zone(self):
        if len(self.zones) >= MAX_ZONES:
            return None
        used = {z["slot"] for z in self.zones}
        slot = next(s for s in range(256) if s not in used)
        self.zones.append({"slot": slot, "raw": b"\xff" * 16 + b"\0" * (2 * self.per_zone), "name": "New zone",
                           "channels": []})
        return slot

    def delete_zone(self, slot):
        self.zones = [z for z in self.zones if z["slot"] != slot]

    def problems(self):
        """Human readable issues that should be fixed before writing."""
        out = []
        for z in self.zones:
            if len(z["channels"]) > self.per_zone:
                out.append("Zone '%s' has %d channels, the limit is %d." % (z["name"], len(z["channels"]), self.per_zone))
        for c in self.channels.values():
            if not c["name"]:
                out.append("Channel %d has no name." % c["number"])
            if c["rx"] <= 0 or c["tx"] <= 0:
                out.append("Channel %d (%s) has no frequency." % (c["number"], c["name"]))
            if c["mode"] == "DMR" and c["contact"] and c["contact"] not in self.contacts:
                out.append("Channel %d (%s) uses a contact that does not exist." % (c["number"], c["name"]))
        for t in self.tglists.values():
            if len(t["contacts"]) > 32:
                out.append("TG list '%s' has more than 32 contacts." % t["name"])
        return out

    # ---- encoding
    def build_image(self):
        img = self.img.copy()
        # channels
        for n in range(1, MAX_CHANNELS + 1):
            hdr, bit, addr = _channel_location(n)
            bits = bytearray(img.get(hdr, 16))
            c = self.channels.get(n)
            _setbit(bits, bit, c is not None)
            img.set(hdr, bytes(bits))
            if c is not None:
                img.set(addr, self._encode_channel(c))
        # contacts
        for i in range(MAX_CONTACTS):
            c = self.contacts.get(i + 1)
            if c is None:
                if img.get(ADDR_CONTACTS + i * 24, 1)[0] != 0xFF:
                    img.set(ADDR_CONTACTS + i * 24, b"\xff" * 24)
                continue
            d = bytearray(c["raw"])
            if _chg(c, "name"):
                d[:16] = _pack_name(c["name"])
            if _chg(c, "id"):
                d[16:20] = bytes.fromhex("%08d" % c["id"])
            if _chg(c, "type"):
                d[20] = CALL_TYPES.index(c["type"])
            img.set(ADDR_CONTACTS + i * 24, bytes(d))
        # TG lists
        lens = bytearray(MAX_TGLISTS)
        for i in range(MAX_TGLISTS):
            t = self.tglists.get(i + 1)
            if t is None:
                if img.get(ADDR_RXGROUP + i * 80, 1)[0] not in (0xFF, 0):
                    img.set(ADDR_RXGROUP + i * 80, b"\xff" * 16 + b"\0" * 64)
                continue
            d = bytearray(t["raw"])
            ids = t["contacts"][:32]
            if _chg(t, "name"):
                d[:16] = _pack_name(t["name"])
            if _chg(t, "contacts"):
                d[16:80] = struct.pack("<32H", *(ids + [0] * (32 - len(ids))))
            img.set(ADDR_RXGROUP + i * 80, bytes(d))
            lens[i] = len(ids)
        if any(_chg(t, "contacts") for t in self.tglists.values()) or any(
                i + 1 not in self.tglists and img.get(ADDR_RXGROUP_LEN + i, 1)[0] for i in range(MAX_TGLISTS)):
            img.set(ADDR_RXGROUP_LEN, bytes(lens))
        # zones
        size = 16 + 2 * self.per_zone
        inuse = bytearray(32)
        for z in self.zones:
            _setbit(inuse, z["slot"], True)
            d = bytearray(z["raw"])
            ids = z["channels"][:self.per_zone]
            if _chg(z, "name"):
                d[:16] = _pack_name(z["name"])
            if _chg(z, "channels"):
                d[16:size] = struct.pack("<%dH" % self.per_zone, *(ids + [0] * (self.per_zone - len(ids))))
            img.set(ADDR_ZONE_LIST + z["slot"] * size, bytes(d))
        img.set(ADDR_ZONE_INUSE, bytes(inuse))
        # general
        g = bytearray(self.general["raw"])
        if _chg(self.general, "callsign"):
            g[:8] = _pack_name(self.general["callsign"], 8)
        if _chg(self.general, "dmrid"):
            g[8:12] = bytes.fromhex("%08d" % self.general["dmrid"])
        img.set(ADDR_GENERAL, bytes(g))
        return img

    @staticmethod
    def _encode_channel(c):
        d = bytearray(c["raw"])
        digital = c["mode"] == "DMR"
        if _chg(c, "name"):
            d[:16] = _pack_name(c["name"])
        if _chg(c, "rx"):
            struct.pack_into("<I", d, 16, int2bcd(int(round(c["rx"] * 100000))))
        if _chg(c, "tx"):
            struct.pack_into("<I", d, 20, int2bcd(int(round(c["tx"] * 100000))))
        if _chg(c, "mode"):
            d[24] = 1 if digital else 0
        if _chg(c, "rxtone"):
            struct.pack_into("<H", d, 32, tone_from_str(c["rxtone"]))
        if _chg(c, "txtone"):
            struct.pack_into("<H", d, 34, tone_from_str(c["txtone"]))
        if _chg(c, "colour"):
            d[44] = c["colour"]
        if _chg(c, "tglist"):
            d[43] = c["tglist"]
        if _chg(c, "contact"):
            struct.pack_into("<H", d, 46, c["contact"])
        if _chg(c, "slot"):
            d[49] = (d[49] | 0x40) if c["slot"] == 2 else (d[49] & ~0x40 & 0xFF)
        f4 = d[51]
        for key, mask, on in (("power", 0x80, c["power"] == "High"), ("bw", 0x02, c["bw"] == "25"),
                              ("rxonly", 0x04, c["rxonly"]), ("zoneskip", 0x20, c["zoneskip"]),
                              ("allskip", 0x10, c["allskip"])):
            if _chg(c, key):
                f4 = (f4 | mask) if on else (f4 & ~mask & 0xFF)
        d[51] = f4
        return bytes(d)

    @staticmethod
    def state(rec):
        """None = untouched, 'new' = added, 'mod' = edited."""
        o = rec.get("_orig")
        if o is None:
            return "new"
        return "mod" if any(_chg(rec, k) for k in o) else None

    def summary(self):
        """{'channels': (added, modified, removed), ...} only for tables with changes."""
        tables = {"channels": self.channels, "contacts": self.contacts, "tglists": self.tglists,
                  "zones": {z["slot"]: z for z in self.zones}}
        out = {}
        for name, t in tables.items():
            added = sum(1 for r in t.values() if self.state(r) == "new")
            mod = sum(1 for r in t.values() if self.state(r) == "mod")
            gone = len(self.orig_keys[name] - set(t))
            if added or mod or gone:
                out[name] = (added, mod, gone)
        if self.state(self.general) == "mod":
            out["identity"] = (0, 1, 0)
        return out
