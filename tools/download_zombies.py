#!/usr/bin/env python3
"""Download Denys Almaral's free demo assets and attach their matching idle clips.

Original files stay unchanged under assets/zombies/source. The generated GLBs
embed the palette, remap animation targets by node name, and convert cm to m by
wrapping the entire original scene (mesh and skeleton) in one scale node.
"""

from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import struct
import tempfile
from urllib.parse import unquote, urljoin, urlparse
from urllib.request import Request, urlopen


ROOT = Path(__file__).resolve().parents[1]
BASE_URL = "https://denysalmaral.com/gamedev/free-zombies/"
SKETCHFAB_URL = (
    "https://sketchfab.com/3d-models/"
    "polyart-zombies-with-animations-free-pack-d9bcfdd88f5348549bc947226af7c314"
)


def sha256(data: bytes) -> str:
    return hashlib.sha256(data).hexdigest()


def atomic_write(path: Path, data: bytes) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temp = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temp, path)
    finally:
        temp.unlink(missing_ok=True)


def json_bytes(value: object) -> bytes:
    return (json.dumps(value, indent=2, sort_keys=True) + "\n").encode()


class Sources:
    def __init__(self, directory: Path, offline: bool, pinned: dict):
        self.directory = directory
        self.offline = offline
        self.pinned = pinned
        self.records: dict[str, dict] = {}

    def read(self, relative: str) -> bytes:
        # All downloaded resources must remain within the creator's demo tree.
        url = urljoin(BASE_URL, relative)
        if not url.startswith(BASE_URL) or urlparse(relative).scheme:
            raise ValueError(f"Asset URL leaves the creator's demo: {relative}")
        relative = unquote(url[len(BASE_URL):])
        if Path(relative).is_absolute() or ".." in Path(relative).parts:
            raise ValueError(f"Unsafe asset path: {relative}")
        path = self.directory / relative
        if not path.resolve().is_relative_to(self.directory.resolve()):
            raise ValueError(f"Asset path leaves the source cache: {relative}")
        if path.is_file():
            data = path.read_bytes()
        elif self.offline:
            raise FileNotFoundError(f"Offline source missing: {path}")
        else:
            request = Request(url, headers={"User-Agent": "project-gdll asset downloader"})
            with urlopen(request, timeout=45) as response:
                data = response.read()
            if not data:
                raise ValueError(f"Empty download: {url}")
            expected = self.pinned.get(relative, {}).get("sha256")
            if expected and sha256(data) != expected:
                raise ValueError(f"Source changed at {url}; review before updating the manifest")
            atomic_write(path, data)
        digest = sha256(data)
        expected = self.pinned.get(relative, {}).get("sha256")
        if expected and digest != expected:
            raise ValueError(f"Cached source does not match manifest: {path}")
        self.records[relative] = {"url": url, "bytes": len(data), "sha256": digest}
        return data

    def gltf(self, relative: str) -> tuple[dict, list[bytes], list[bytes]]:
        doc = json.loads(self.read(relative))
        if doc.get("asset", {}).get("version") != "2.0":
            raise ValueError(f"Expected glTF 2.0: {relative}")
        base = str(Path(relative).parent) + "/"
        buffers = [self.read(base + buffer["uri"]) for buffer in doc["buffers"]]
        for spec, data in zip(doc["buffers"], buffers):
            if len(data) != spec["byteLength"]:
                raise ValueError(f"Buffer length mismatch: {relative}")
        # The official demo replaces each imported material's image with this
        # palette at the demo root; the glTF-relative image URLs return 404.
        images = [self.read("zcolors.png" if image["uri"] == "zcolors.png" else base + image["uri"])
                  for image in doc.get("images", [])]
        return doc, buffers, images


def parents_by_name(nodes: list[dict]) -> dict[str, str | None]:
    names = [node["name"] for node in nodes]
    if len(set(names)) != len(names):
        raise ValueError("Skeleton node names must be unique")
    parents: dict[str, str | None] = dict.fromkeys(names)
    for node in nodes:
        for child in node.get("children", []):
            if parents[names[child]] is not None:
                raise ValueError("A skeleton node has multiple parents")
            parents[names[child]] = node["name"]
    return parents


def merge_idle(rig_data: tuple, idle_data: tuple) -> bytes:
    rig, rig_buffers, rig_images = rig_data
    idle, idle_buffers, _ = idle_data
    result = copy.deepcopy(rig)
    nodes_by_name = {node["name"]: i for i, node in enumerate(rig["nodes"])}
    rig_parents = parents_by_name(rig["nodes"])
    idle_parents = parents_by_name(idle["nodes"])
    if rig_parents != idle_parents:
        raise ValueError("Idle animation and character have incompatible skeleton hierarchies")
    if len(idle.get("animations", [])) != 1:
        raise ValueError("Expected one named idle animation")
    animation = copy.deepcopy(idle["animations"][0])
    if "idle" not in animation.get("name", "").lower():
        raise ValueError("Animation is not named idle")

    binary = bytearray()

    def append(data: bytes) -> int:
        binary.extend(b"\0" * (-len(binary) % 4))
        offset = len(binary)
        binary.extend(data)
        return offset

    offsets = [append(data) for data in rig_buffers]
    for view in result.get("bufferViews", []):
        view["byteOffset"] = offsets[view["buffer"]] + view.get("byteOffset", 0)
        view["buffer"] = 0

    copied_views: dict[int, int] = {}
    copied_accessors: dict[int, int] = {}

    def copy_accessor(index: int) -> int:
        if index in copied_accessors:
            return copied_accessors[index]
        accessor = copy.deepcopy(idle["accessors"][index])
        if "sparse" in accessor:
            raise ValueError("Sparse animation accessors are not supported by this pack importer")
        view_index = accessor["bufferView"]
        if view_index not in copied_views:
            view = copy.deepcopy(idle["bufferViews"][view_index])
            start = view.get("byteOffset", 0)
            source = idle_buffers[view["buffer"]]
            end = start + view["byteLength"]
            if end > len(source):
                raise ValueError("Animation buffer view exceeds its source")
            view["byteOffset"] = append(source[start:end])
            view["buffer"] = 0
            copied_views[view_index] = len(result["bufferViews"])
            result["bufferViews"].append(view)
        accessor["bufferView"] = copied_views[view_index]
        copied_accessors[index] = len(result["accessors"])
        result["accessors"].append(accessor)
        return copied_accessors[index]

    for sampler in animation["samplers"]:
        sampler["input"] = copy_accessor(sampler["input"])
        sampler["output"] = copy_accessor(sampler["output"])
    for channel in animation["channels"]:
        name = idle["nodes"][channel["target"]["node"]]["name"]
        channel["target"]["node"] = nodes_by_name[name]
    result["animations"] = [animation]

    for image, data in zip(result.get("images", []), rig_images):
        if not data.startswith(b"\x89PNG\r\n\x1a\n"):
            raise ValueError("Expected the creator's PNG palette")
        image.pop("uri")
        image["mimeType"] = "image/png"
        image["bufferView"] = len(result["bufferViews"])
        result["bufferViews"].append({"buffer": 0, "byteOffset": append(data), "byteLength": len(data)})

    # Preserve the original scene transform and inverse bind matrices together.
    for scene in result["scenes"]:
        root = len(result["nodes"])
        result["nodes"].append({"name": "centimeters_to_meters", "scale": [0.01] * 3,
                                "children": scene["nodes"]})
        scene["nodes"] = [root]
    result["buffers"] = [{"byteLength": len(binary)}]
    result["asset"]["copyright"] = "Denys Almaral; see ../README.md and ../sources.json"
    result["asset"]["extras"] = {"source": BASE_URL, "preparation": "tools/download_zombies.py"}
    payload = json.dumps(result, separators=(",", ":")).encode()
    payload += b" " * (-len(payload) % 4)
    binary.extend(b"\0" * (-len(binary) % 4))
    total = 12 + 8 + len(payload) + 8 + len(binary)
    return (struct.pack("<4sII", b"glTF", 2, total) + struct.pack("<I4s", len(payload), b"JSON")
            + payload + struct.pack("<I4s", len(binary), b"BIN\0") + binary)


def prepare_pack(output_dir: Path, offline: bool) -> None:
    """Validate every source and merge before replacing any published model."""
    manifest_path = output_dir / "sources.json"
    old = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
    sources = Sources(output_dir / "source", offline, old.get("files", {}))
    outputs = {}
    prepared: dict[Path, bytes] = {}
    for gender in ("Female", "Male"):
        prefix = f"Zombie{gender}"
        idle = sources.gltf(f"animations-gltf/gltf_{prefix}/{prefix}@idle_220f.gltf")
        for letter in "ABCDE":
            name = f"{prefix}_{letter}"
            rig = sources.gltf(f"rig-gltf_joined/{name}_joined.gltf")
            data = merge_idle(rig, idle)
            output = output_dir / "models" / f"{name}.glb"
            prepared[output] = data
            outputs[f"models/{name}.glb"] = {"bytes": len(data), "sha256": sha256(data),
                                             "animation": idle[0]["animations"][0]["name"]}
    manifest = {"schema_version": 1, "creator": "Denys Almaral", "title": "Polyart Zombies with Animations Free Pack",
                "sketchfab_url": SKETCHFAB_URL, "demo_url": BASE_URL,
                "fab_url": "https://www.fab.com/listings/2a85aa17-6eae-4c9d-b805-aeb1d722962f",
                "license": {"name": "Free Standard", "url": "https://sketchfab.com/licenses"},
                "files": sources.records, "outputs": outputs}
    # All network, schema, hash, and skeleton checks are complete. Each model
    # replacement is atomic; failed downloads never publish a partial pack.
    for output, data in prepared.items():
        atomic_write(output, data)
        print(f"Prepared {output} ({len(data):,} bytes)", flush=True)
    atomic_write(manifest_path, json_bytes(manifest))


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--offline", action="store_true", help="Rebuild from cached originals without networking")
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/zombies")
    args = parser.parse_args()
    prepare_pack(args.output_dir, args.offline)


if __name__ == "__main__":
    main()
