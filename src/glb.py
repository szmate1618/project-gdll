"""Read and pack self-contained GLB documents without changing binary payloads."""
from __future__ import annotations

import json
import struct


def read_glb(data: bytes):
    if len(data) < 28 or struct.unpack_from("<4sII", data) != (b"glTF", 2, len(data)):
        raise ValueError("Expected a complete GLB 2.0 map")
    chunks = []
    cursor = 12
    while cursor < len(data):
        if cursor + 8 > len(data):
            raise ValueError("Truncated GLB chunk header")
        size, kind = struct.unpack_from("<I4s", data, cursor)
        cursor += 8
        if size % 4 or cursor + size > len(data):
            raise ValueError("Invalid GLB chunk size")
        chunks.append((kind, data[cursor:cursor + size]))
        cursor += size
    if [kind for kind, _ in chunks] != [b"JSON", b"BIN\0"]:
        raise ValueError("Expected one JSON and one embedded binary chunk")
    document = json.loads(chunks[0][1])
    if len(document.get("buffers", [])) != 1 or "uri" in document["buffers"][0]:
        raise ValueError("Expected one self-contained GLB buffer")
    length = document["buffers"][0]["byteLength"]
    if not length <= len(chunks[1][1]) <= length + 3:
        raise ValueError("Invalid embedded buffer length")
    return document, chunks[1][1][:length]



def pack_glb(document, binary):
    text = json.dumps(document, separators=(",", ":"), ensure_ascii=False, allow_nan=False).encode()
    text += b" " * (-len(text) % 4)
    binary += b"\0" * (-len(binary) % 4)
    size = 12 + 8 + len(text) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, size) + struct.pack("<I4s", len(text), b"JSON") + text
            + struct.pack("<I4s", len(binary), b"BIN\0") + binary)

