"""Preserve skin targets and binary data while preparing the creator's pack."""

import base64
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch
from urllib.parse import quote

from tools.download_zombies import Sources, merge_idle, prepare_pack


def read_glb(data):
    magic, version, length = struct.unpack_from("<4sII", data)
    if (magic, version, length) != (b"glTF", 2, len(data)):
        raise AssertionError("Invalid GLB header")
    json_size, json_kind = struct.unpack_from("<I4s", data, 12)
    if json_kind != b"JSON" or json_size % 4:
        raise AssertionError("Invalid JSON chunk")
    document = json.loads(data[20:20 + json_size])
    size, kind = struct.unpack_from("<I4s", data, 20 + json_size)
    binary = data[28 + json_size:]
    if kind != b"BIN\0" or size != len(binary) or size % 4:
        raise AssertionError("Invalid binary chunk")
    return document, binary


def view_bytes(document, binary, index):
    view = document["bufferViews"][index]
    start = view.get("byteOffset", 0)
    return binary[start:start + view["byteLength"]]


def fixture():
    positions = struct.pack("<9f", 0, 0, 0, 100, 0, 0, 0, 180, 0)
    indices = struct.pack("<3H", 0, 1, 2)
    bind_matrix = struct.pack("<16f", 1, 0, 0, 0, 0, 1, 0, 0,
                              0, 0, 1, 0, 0, 0, 0, 1)
    rig_buffers = [b"mesh" + positions + indices + b"pad", b"skin" + bind_matrix * 2]
    png = base64.b64decode(
        "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAQAAAC1HAwCAAAAC0lEQVR42mP8/x8AAwMCAO+jY1sAAAAASUVORK5CYII=")
    rig = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"name": "Root", "children": [2, 3]}, {"name": "Spine"},
                  {"name": "Hip", "children": [1]}, {"name": "Body", "mesh": 0, "skin": 0}],
        "buffers": [{"uri": "mesh.bin", "byteLength": len(rig_buffers[0])},
                    {"uri": "skin.bin", "byteLength": len(rig_buffers[1])}],
        "bufferViews": [{"buffer": 0, "byteOffset": 4, "byteLength": len(positions)},
                        {"buffer": 0, "byteOffset": 40, "byteLength": len(indices)},
                        {"buffer": 1, "byteOffset": 4, "byteLength": 128}],
        "accessors": [{"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
                      {"bufferView": 1, "componentType": 5123, "count": 3, "type": "SCALAR"},
                      {"bufferView": 2, "componentType": 5126, "count": 2, "type": "MAT4"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "skins": [{"joints": [2, 1], "skeleton": 2, "inverseBindMatrices": 2}],
        "images": [{"uri": "palette.png"}],
        "textures": [{"source": 0}],
    }
    # Same named hierarchy, deliberately different node order. Both accessors
    # share a buffer view and have offsets inside it; the view itself is offset.
    times = struct.pack("<2f", 0, 1)
    movement = struct.pack("<6f", 0, 100, 0, 0, 110, 0)
    idle_view = b"skip" + times + movement
    idle_buffers = [b"prefix00" + idle_view + b"trailer"]
    idle = {
        "asset": {"version": "2.0"},
        "nodes": [{"name": "Body"}, {"name": "Hip", "children": [3]},
                  {"name": "Root", "children": [1, 0]}, {"name": "Spine"}],
        "bufferViews": [{"buffer": 0, "byteOffset": 8, "byteLength": len(idle_view)}],
        "accessors": [{"bufferView": 0, "byteOffset": 4, "componentType": 5126,
                       "count": 2, "type": "SCALAR", "min": [0], "max": [1]},
                      {"bufferView": 0, "byteOffset": 12, "componentType": 5126,
                       "count": 2, "type": "VEC3"}],
        "animations": [{"name": "Zombie@idle_220f", "samplers": [
            {"input": 0, "output": 1, "interpolation": "LINEAR"},
            {"input": 0, "output": 1, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 3, "path": "translation"}},
                         {"sampler": 1, "target": {"node": 1, "path": "translation"}}]}],
    }
    return (rig, rig_buffers, [png]), (idle, idle_buffers, [])


class ZombieAssetTests(unittest.TestCase):
    def test_targets_bind_matrices_and_binary_offsets_survive_merge(self):
        rig, idle = fixture()
        original = copy.deepcopy((rig, idle))
        data = merge_idle(rig, idle)
        document, binary = read_glb(data)
        self.assertEqual((rig, idle), original, "Preparing one variant must not mutate shared source data")
        self.assertEqual(data, merge_idle(rig, idle), "Offline preparation must be deterministic")
        self.assertEqual(document["skins"], rig[0]["skins"])
        self.assertEqual(document["nodes"][:4], rig[0]["nodes"])
        self.assertEqual(document["nodes"][document["scenes"][0]["nodes"][0]]["children"], [0])
        self.assertEqual(document["nodes"][-1]["scale"], [0.01, 0.01, 0.01])
        self.assertEqual(len(document["buffers"]), 1)
        self.assertNotIn("uri", document["buffers"][0])
        self.assertIn(len(binary) - document["buffers"][0]["byteLength"], range(4))
        for i, view in enumerate(rig[0]["bufferViews"]):
            start = view.get("byteOffset", 0)
            expected = rig[1][view["buffer"]][start:start + view["byteLength"]]
            self.assertEqual(view_bytes(document, binary, i), expected)
        animation = document["animations"][0]
        self.assertEqual([c["target"]["node"] for c in animation["channels"]], [1, 2])
        self.assertEqual(animation["samplers"][0], animation["samplers"][1])
        input_item = document["accessors"][animation["samplers"][0]["input"]]
        output_item = document["accessors"][animation["samplers"][0]["output"]]
        self.assertEqual(input_item["bufferView"], output_item["bufferView"])
        sample_view = view_bytes(document, binary, input_item["bufferView"])
        self.assertEqual(struct.unpack_from("<2f", sample_view, input_item["byteOffset"]), (0, 1))
        self.assertEqual(struct.unpack_from("<6f", sample_view, output_item["byteOffset"]),
                         (0, 100, 0, 0, 110, 0))
        self.assertEqual(len(document["accessors"]), len(rig[0]["accessors"]) + 2)
        image = document["images"][0]
        self.assertNotIn("uri", image)
        self.assertEqual(image["mimeType"], "image/png")
        self.assertEqual(view_bytes(document, binary, image["bufferView"]), rig[2][0])
        for view in document["bufferViews"]:
            self.assertEqual(view["buffer"], 0)
            self.assertEqual(view["byteOffset"] % 4, 0)
            self.assertLessEqual(view["byteOffset"] + view["byteLength"], document["buffers"][0]["byteLength"])

    def test_wrong_hierarchy_or_ambiguous_names_cannot_retarget(self):
        rig, idle = fixture()
        idle[0]["nodes"][1]["children"] = []
        idle[0]["nodes"][2]["children"].append(3)
        with self.assertRaisesRegex(ValueError, "incompatible"):
            merge_idle(rig, idle)
        rig, idle = fixture()
        rig[0]["nodes"][1]["name"] = "Hip"
        with self.assertRaisesRegex(ValueError, "unique"):
            merge_idle(rig, idle)

    def test_truncated_animation_view_and_wrong_clip_fail(self):
        rig, idle = fixture()
        idle[0]["bufferViews"][0]["byteLength"] += 100
        with self.assertRaisesRegex(ValueError, "exceeds"):
            merge_idle(rig, idle)
        rig, idle = fixture()
        idle[0]["animations"][0]["name"] = "Walk"
        with self.assertRaisesRegex(ValueError, "idle"):
            merge_idle(rig, idle)

    def test_offline_cache_checks_provenance_and_preserves_bytes(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            relative = "rig-gltf_joined/model.bin"
            path = root / relative
            path.parent.mkdir()
            data = b"original source bytes"
            path.write_bytes(data)
            pinned = {relative: {"sha256": hashlib.sha256(data).hexdigest()}}
            sources = Sources(root, True, pinned)
            self.assertEqual(sources.read(relative), data)
            self.assertEqual(sources.records[relative]["sha256"], pinned[relative]["sha256"])
            path.write_bytes(b"modified")
            with self.assertRaisesRegex(ValueError, "manifest"):
                sources.read(relative)
            self.assertEqual(path.read_bytes(), b"modified")
            with self.assertRaises(FileNotFoundError):
                sources.read("missing.bin")

    def test_encoded_absolute_path_cannot_escape_source_cache(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            cache = root / "cache"
            cache.mkdir()
            outside = root / "outside.bin"
            outside.write_bytes(b"not in the source cache")
            sources = Sources(cache, True, {})
            with self.assertRaises(ValueError):
                sources.read(quote(str(outside), safe=""))
            with self.assertRaises(ValueError):
                sources.read("%2e%2e/outside.bin")
            (cache / "link").symlink_to(root, target_is_directory=True)
            with self.assertRaises(ValueError):
                sources.read("link/outside.bin")

    def test_late_download_failure_preserves_complete_publication_boundary(self):
        for existing in (False, True):
            with self.subTest(existing=existing), tempfile.TemporaryDirectory() as temporary:
                root = Path(temporary)
                model = root / "models" / "ZombieFemale_A.glb"
                manifest = root / "sources.json"
                if existing:
                    model.parent.mkdir()
                    model.write_bytes(b"previous good model")
                    manifest.write_text('{"files": {}, "outputs": {"previous": "good"}}\n')
                before = {path.relative_to(root): path.read_bytes()
                          for path in root.rglob("*") if path.is_file()}
                rig, idle = fixture()

                def read_source(relative):
                    # Several characters already prepared successfully when a
                    # later source fails. None may become a published subset.
                    if "ZombieMale_D" in relative:
                        raise OSError("synthetic late download failure")
                    return idle if "animations-gltf" in relative else rig

                with patch("tools.download_zombies.Sources") as source_type:
                    source_type.return_value.gltf.side_effect = read_source
                    source_type.return_value.records = {}
                    with self.assertRaisesRegex(OSError, "late download"):
                        prepare_pack(root, offline=True)
                after = {path.relative_to(root): path.read_bytes()
                         for path in root.rglob("*") if path.is_file()}
                self.assertEqual(after, before,
                                 "Failed preparation must preserve old models and matching provenance")


if __name__ == "__main__":
    unittest.main()
