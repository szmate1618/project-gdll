"""Build the bundled outline font from the original menu's 5x7 glyphs.

Uses only the standard library. Each lit pixel becomes a square outline; an em
is ten original pixels, and each character advances six pixels. Lowercase maps
to the original uppercase shapes. No hinting or external font is substituted.
Run from the repository root; the checked-in TTF is also shipped in game ZIPs.
"""
from pathlib import Path
import json
import struct


def pack(fmt, *values):
    return struct.pack(">" + fmt, *values)


def checksum(data):
    data += b"\0" * (-len(data) % 4)
    return sum(struct.unpack(">" + "I" * (len(data) // 4), data)) & 0xffffffff


def glyph(columns):
    points = []
    for x, column in enumerate(columns):
        for y in range(7):
            if column & (1 << y):
                left, bottom = x * 100, (6 - y) * 100
                points.extend(((left, bottom), (left, bottom + 100),
                               (left + 100, bottom + 100), (left + 100, bottom)))
    if not points:
        return pack("hhhhh", 0, 0, 0, 0, 0), 0
    contours = len(points) // 4
    data = pack("hhhhh", contours, min(x for x, y in points), min(y for x, y in points),
                max(x for x, y in points), max(y for x, y in points))
    data += pack("H" * contours, *(4 * i + 3 for i in range(contours)))
    data += pack("H", 0)  # No instructions; keep the authored square outlines.
    data += b"\x01" * len(points)  # On-curve, signed 16-bit coordinate deltas.
    for axis in (0, 1):
        previous = 0
        for point in points:
            data += pack("h", point[axis] - previous)
            previous = point[axis]
    return data, len(points)


def build_font(glyphs):
    shapes = [[0, 2, 1, 2, 0]] + list(glyphs.values())  # Original unknown-character marker.
    ids = {char: index + 1 for index, char in enumerate(glyphs)}
    count = len(shapes)
    outlines, offsets, largest = b"", [], 0
    for shape in shapes:
        offsets.append(len(outlines))
        data, points = glyph(shape)
        largest = max(largest, points)
        outlines += data + b"\0" * (-len(data) % 4)
    offsets.append(len(outlines))

    codes = list(range(32, 127)) + [0xffff]
    segments = len(codes)
    search = 2 ** (segments.bit_length() - 1)
    cmap = pack("7H", 4, 16 + segments * 8, 0, segments * 2,
                search * 2, search.bit_length() - 1, segments * 2 - search * 2)
    cmap += pack("H" * segments, *codes) + pack("H", 0)
    cmap += pack("H" * segments, *codes)
    cmap += pack("H" * segments, *[
        ((ids.get(chr(code).upper(), 0) - code) & 0xffff) if code != 0xffff else 1
        for code in codes])
    cmap += b"\0" * (segments * 2)

    names = {1: "Menu Pixel", 2: "Regular", 3: "Godollo Menu Pixel 1.0",
             4: "Menu Pixel Regular", 5: "Version 1.0", 6: "MenuPixel-Regular"}
    records, strings = b"", b""
    for name_id, value in names.items():
        encoded = value.encode("utf-16-be")
        records += pack("6H", 3, 1, 0x409, name_id, len(encoded), len(strings))
        strings += encoded
    os2 = bytearray(78)
    struct.pack_into(">HhHH", os2, 0, 0, 600, 400, 5)
    struct.pack_into(">4sHHHhhhHH", os2, 58, b"GDLL", 64, 32, 126, 700, -300, 0, 700, 300)
    tables = {
        "OS/2": bytes(os2),
        "cmap": pack("HHHHI", 0, 1, 3, 1, 12) + cmap,
        "glyf": outlines,
        "head": pack("IIIIHHQQhhhhHHhhh", 0x10000, 0x10000, 0, 0x5f0f3cf5,
                     3, 1000, 0, 0, 0, 0, 500, 700, 0, 10, 2, 1, 0),
        "hhea": pack("I16h", 0x10000, 700, -300, 0, 600, 0, 100, 500,
                     1, 0, 0, 0, 0, 0, 0, 0, count),
        "hmtx": b"".join(pack("Hh", 600, next((x * 100 for x, bits in enumerate(shape) if bits), 0))
                         for shape in shapes),
        "loca": pack("I" * len(offsets), *offsets),
        "maxp": pack("I14H", 0x10000, count, largest, largest // 4,
                     0, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0),
        "name": pack("3H", 0, len(names), 6 + len(records)) + records + strings,
        "post": pack("IIhhIIIII", 0x30000, 0, -100, 100, 1, 0, 0, 0, 0),
    }
    search = 2 ** (len(tables).bit_length() - 1)
    directory = pack("I4H", 0x10000, len(tables), search * 16,
                     search.bit_length() - 1, len(tables) * 16 - search * 16)
    payload, head_offset = b"", 0
    for tag, data in sorted(tables.items()):
        offset = 12 + len(tables) * 16 + len(payload)
        directory += pack("4sIII", tag.encode(), checksum(data), offset, len(data))
        if tag == "head":
            head_offset = offset
        payload += data + b"\0" * (-len(data) % 4)
    result = bytearray(directory + payload)
    struct.pack_into(">I", result, head_offset + 8, (0xb1b0afba - checksum(bytes(result))) & 0xffffffff)
    return result


if __name__ == "__main__":
    directory = Path(__file__).resolve().parents[1] / "ui/fonts"
    glyphs = json.loads((directory / "menu-pixel.json").read_text())
    output = directory / "menu-pixel.ttf"
    output.write_bytes(build_font(glyphs))
    print(output)
