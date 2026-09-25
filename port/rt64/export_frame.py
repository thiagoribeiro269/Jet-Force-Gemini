"""Encode a displayed framebuffer as opaque RGB, preserving its RGB bytes.

The RGBA readback alpha is internal RDP render-target data, not PNG display
opacity. In the observed character frame most drawn pixels have alpha 7/255.
Publishing that as PNG alpha makes the character almost invisible in clients
that composite transparency (including WhatsApp). Keep raw RGBA for diagnostics;
use an RGB PNG for the opaque final framebuffer view.
"""
import struct
import zlib


def framebuffer_png(width, height, rgba):
    if width <= 0 or height <= 0 or len(rgba) != width * height * 4:
        raise ValueError("Invalid framebuffer dimensions or length")

    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data))

    rows = []
    for y in range(height):
        row = b"".join(rgba[(y * width + x) * 4:(y * width + x) * 4 + 3] for x in range(width))
        rows.append(b"\0" + row)
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(b"".join(rows))) + chunk(b"IEND", b""))
