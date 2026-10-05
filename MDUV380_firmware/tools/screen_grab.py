#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""Grab the screen of a running OpenGD77 MDUV380 over its USB serial port and save it as a PNG.

Uses the firmware's CPS "read display buffer" command ('R', area 6): works while the radio is in
normal operation, no need to enter the CPS screen. The display is 160x128, RGB565 big endian.

    python screen_grab.py out.png                 # first OpenGD77 port found
    python screen_grab.py out.png -p /dev/ttyACM0 -s 4

Needs: pyserial, pillow.
"""
import argparse
import struct
import sys

import serial
from serial.tools import list_ports
from PIL import Image

WIDTH, HEIGHT = 160, 128
AREA_DISPLAY_BUFFER = 6
CHUNK = 256


def find_port():
    for p in list_ports.comports():
        if "opengd77" in (p.manufacturer or "").lower() or "opengd77" in (p.description or "").lower() or "opengd77" in (p.hwid or "").lower():
            return p.device
    return None


def grab(port_name):
    total = WIDTH * HEIGHT * 2
    buf = bytearray()
    with serial.Serial(port_name, 115200, timeout=2) as port:
        port.reset_input_buffer()
        addr = 0
        while addr < total:
            n = min(CHUNK, total - addr)
            port.write(b"R" + bytes([AREA_DISPLAY_BUFFER]) + struct.pack(">IH", addr, n))
            hdr = port.read(3)
            if len(hdr) < 3 or hdr[0:1] != b"R":
                raise IOError("bad reply %r (is the radio in hotspot or DFU mode?)" % hdr)
            length = (hdr[1] << 8) | hdr[2]
            data = port.read(length)
            if len(data) != length:
                raise IOError("short read: %d of %d bytes" % (len(data), length))
            buf += data
            addr += length
    return buf


def to_image(buf, scale):
    img = Image.new("RGB", (WIDTH, HEIGHT))
    px = img.load()
    for i in range(WIDTH * HEIGHT):
        v = (buf[2 * i] << 8) | buf[2 * i + 1]
        r, g, b = (v >> 11) & 31, (v >> 5) & 63, v & 31
        px[i % WIDTH, i // WIDTH] = ((r << 3) | (r >> 2), (g << 2) | (g >> 4), (b << 3) | (b >> 2))
    return img.resize((WIDTH * scale, HEIGHT * scale), Image.NEAREST) if scale > 1 else img


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("output", help="PNG file to write")
    ap.add_argument("-p", "--port", help="serial port (default: auto-detect an OpenGD77 radio)")
    ap.add_argument("-s", "--scale", type=int, default=4, help="integer upscale factor (default 4)")
    args = ap.parse_args()

    port = args.port or find_port()
    if not port:
        sys.exit("No OpenGD77 serial port found, use -p")

    to_image(grab(port), args.scale).save(args.output)
    print("saved", args.output)


if __name__ == "__main__":
    main()
