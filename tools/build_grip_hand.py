"""Build the static, translucent Set grip hand GLB from its tracked JSON, offline."""
import argparse
import json
from pathlib import Path
import struct

from build_weapon_assets import build_model


def build_grip_hand(source):
    document = json.loads(source.read_text())
    if document.get("version") != 1 or document.get("id") != "grip_hand" or document.get("units") != "meters":
        raise ValueError("Unsupported grip hand mesh source")
    if document.get("contact_point") != [0, 0, 0] or document.get("contact_direction") != [0, 0, 1]:
        raise ValueError("Grip hand must use its palm-contact frame")
    if document.get("alpha") != .5:
        raise ValueError("Set grip requires 50 percent opacity")
    model = build_model(document)
    size, _ = struct.unpack_from("<II", model, 12)
    data = json.loads(model[20:20 + size])
    binary = model[20 + size:]
    data["asset"]["generator"] = "GDL Set grip builder"
    for material in data["materials"]:
        material["pbrMetallicRoughness"]["baseColorFactor"] = [.05, .9, .18, .5]
        material["alphaMode"] = "BLEND"
    text = json.dumps(data, separators=(",", ":"), allow_nan=False).encode()
    text += b" " * (-len(text) % 4)
    return (struct.pack("<4sII", b"glTF", 2, 20 + len(text) + len(binary)) +
            struct.pack("<I4s", len(text), b"JSON") + text + binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    model = build_grip_hand(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    temporary = args.output.with_suffix(".tmp.glb")
    temporary.write_bytes(model)
    temporary.replace(args.output)


if __name__ == "__main__":
    main()
