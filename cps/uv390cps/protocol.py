"""Serial CPS protocol of the OpenGD77 firmware (see application/source/usb/usb_com.c).

Request  : 'R', area, addr (u32 BE), length (u16 BE)
Reply    : 'R', length (u16 BE), data
Areas    : 1 = SPI flash (raw address), 2 = EEPROM (same as flash offset 0 on MDUV380), 9 = radio info
This module is read-only on purpose: there is no write support yet.
"""
import struct

import serial
from serial.tools import list_ports

AREA_FLASH = 1
AREA_EEPROM = 2
AREA_RADIO_INFO = 9
CHUNK = 256
SECTOR = 4096
WRITE_CHUNK = 128

RADIO_TYPES = {0: "GD-77", 1: "GD-77S", 2: "DM-1801", 3: "RD-5R", 4: "DM-1801A", 5: "MD-9600",
               6: "MD-UV380 / UV390", 7: "MD-380", 8: "DM-1701 / RT-84", 9: "MD-2017", 10: "DM-1701 (RGB)"}


class RadioError(Exception):
    pass


def find_ports():
    """Serial ports that look like an OpenGD77 radio first, then all the others."""
    ports = list(list_ports.comports())

    def is_gd(p):
        text = " ".join((p.manufacturer or "", p.description or "", p.product or "", p.hwid or "")).lower()
        return "opengd77" in text or "openmd" in text or "0483:5740" in text

    return sorted(ports, key=lambda p: (not is_gd(p), p.device))


class Radio:
    def __init__(self, port_name):
        try:
            self.port = serial.Serial(port_name, 115200, timeout=2)
        except serial.SerialException as e:
            raise RadioError(str(e))
        self.port.reset_input_buffer()

    def close(self):
        self.port.close()

    def read(self, area, addr, length, progress=None):
        out = bytearray()
        total = length
        while length > 0:
            n = min(CHUNK, length)
            self.port.write(b"R" + bytes([area]) + struct.pack(">IH", addr, n))
            hdr = self.port.read(3)
            if len(hdr) < 3 or hdr[0:1] != b"R":
                raise RadioError("bad reply %r (hotspot or DFU mode?)" % hdr)
            got = (hdr[1] << 8) | hdr[2]
            data = self.port.read(got)
            if got == 0 or len(data) != got:
                raise RadioError("short read at 0x%X" % addr)
            out += data
            addr += got
            length -= got
            if progress:
                progress(len(out), total)
        return bytes(out)

    def info(self):
        d = self.read(AREA_RADIO_INFO, 0, 46)
        version, rtype = struct.unpack_from("<II", d, 0)
        git = d[8:24].split(b"\0")[0].decode("ascii", "replace")
        built = d[24:40].split(b"\0")[0].decode("ascii", "replace")
        return {"version": version, "type": RADIO_TYPES.get(rtype, "type %d" % rtype),
                "git": git, "built": built}


    # ------------------------------------------------------------ writing
    def _x(self, payload, what):
        self.port.write(b"X" + payload)
        r = self.port.read(2)
        if r[:1] != b"X":
            raise RadioError("radio refused: %s" % what)

    def _command(self, *args):
        self.port.write(b"C" + bytes(args))
        self.port.read(1)

    def begin_write(self):
        """Shows the CPS screen on the radio (stops normal operation while we write)."""
        self._command(0)

    def write_spans(self, spans, progress=None):
        """Write changed byte spans. The radio reads each 4 KiB sector first, so bytes we do not send stay."""
        by = {}
        for addr, data in spans:
            off = 0
            while off < len(data):
                a = addr + off
                n = min(len(data) - off, SECTOR - a % SECTOR)
                by.setdefault(a // SECTOR, []).append((a, data[off:off + n]))
                off += n
        for k, sector in enumerate(sorted(by)):
            self._x(bytes([1]) + sector.to_bytes(3, "big"), "prepare sector 0x%X" % (sector * SECTOR))
            for addr, data in by[sector]:
                for off in range(0, len(data), WRITE_CHUNK):
                    part = data[off:off + WRITE_CHUNK]
                    self._x(bytes([2]) + struct.pack(">IH", addr + off, len(part)) + part, "send data 0x%X" % (addr + off))
            self._x(bytes([3]), "write sector 0x%X" % (sector * SECTOR))
            if progress:
                progress(k + 1, len(by))

    def reboot(self):
        """Plain reboot without touching any caches (used after a failed write)."""
        self._command(6, 1)

    def finish_and_reboot(self):
        """The radio rebuilds its channel caches, saves its settings and reboots."""
        self._command(6, 0)
