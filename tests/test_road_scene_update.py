"""Road-only map patches preserve geometry, frames and fingerprint guards."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import patch

import numpy as np

from src.glb import pack_glb, read_glb
from src.road_scene_update import prepare_road_offset_patch, update_road_offset


def fixture():
    parent = np.array([[2., 0, 0, 4], [0, 0, 3., 9], [0, -1., 0, 7], [0, 0, 0, 1.]])
    document = {
        "asset": {"version": "2.0"}, "buffers": [{"byteLength": 4}],
        "scenes": [{"nodes": [0], "extras": {"roads": {"offset_m": .25}}}],
        "nodes": [{"matrix": parent.ravel(order="F").tolist(), "children": [1, 2]},
                  {"name": "roads_asphalt", "mesh": 0, "translation": [1., 2., 3.]},
                  {"name": "terrain", "mesh": 1}],
        "meshes": [{"name": "roads_asphalt", "extras": {"kind": "road"}, "primitives": []},
                   {"name": "terrain", "primitives": []}],
    }
    return document, pack_glb(document, b"keep"), parent


class RoadOffsetPatchTests(unittest.TestCase):
    def test_only_road_nodes_move_in_world_up(self):
        original, data, parent = fixture()
        updated, binary = read_glb(prepare_road_offset_patch(data, .25, .02))
        self.assertEqual(binary, b"keep")
        self.assertEqual(updated["meshes"], original["meshes"])
        self.assertEqual(updated["nodes"][0], original["nodes"][0])
        self.assertEqual(updated["nodes"][2], original["nodes"][2])
        local = np.array(updated["nodes"][1]["matrix"]).reshape(4, 4, order="F")
        before = parent @ [1., 2., 3., 1.]
        after = parent @ local[:, 3]
        np.testing.assert_allclose(after - before, [0, -.23, 0, 0], atol=1e-12)
        np.testing.assert_allclose(local[:3, :3], np.eye(3))
        self.assertEqual(updated["scenes"][0]["extras"]["roads"]["offset_m"], .02)
        with self.assertRaisesRegex(ValueError, "Recorded road offset"):
            prepare_road_offset_patch(pack_glb(updated, binary), .25, .02)

    def test_road_parent_refused_and_source_offset_required(self):
        doc, _, _ = fixture()
        doc["nodes"][1]["children"] = [2]
        with self.assertRaisesRegex(ValueError, "leaves"):
            prepare_road_offset_patch(pack_glb(doc, b"keep"), .25, .02)
        for invalid in (float("nan"), -.1, .001):
            with self.assertRaises(ValueError):
                prepare_road_offset_patch(fixture()[1], .25, invalid)

    def test_publication_preserves_placements_and_rejects_stale_companion(self):
        _, data, _ = fixture()
        terrain = SimpleNamespace(heights=np.zeros((2, 2)), x_axis=np.arange(2),
                                  y_axis=np.arange(2), datum=0)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            town, manifest = root / "town.glb", root / "trees.json"
            town.write_bytes(data)
            original = {"map": {"map_sha256": hashlib.sha256(data).hexdigest()}, "instances": [[1, 2, 3]]}
            manifest.write_text(json.dumps(original))
            with patch("src.road_scene_update.load_tree_map", return_value=(terrain, {}, {})), \
                    patch("src.road_scene_update.inspect_glb", return_value={"valid": True}):
                result = update_road_offset(town, {"roads": {"offset_m": .02}}, .25, [manifest])
                updated = json.loads(manifest.read_text())
                self.assertEqual(updated["instances"], original["instances"])
                self.assertEqual(updated["map"]["map_sha256"], hashlib.sha256(town.read_bytes()).hexdigest())
                self.assertEqual((Path(result["backup"]) / "0-town.glb").read_bytes(), data)
                stale = copy.deepcopy(original)
                manifest.write_text(json.dumps(stale))
                good_map = town.read_bytes()
                with self.assertRaisesRegex(ValueError, "stale or unrelated"):
                    update_road_offset(town, {"roads": {"offset_m": .03}}, .02, [manifest])
                self.assertEqual(town.read_bytes(), good_map)
                self.assertEqual(json.loads(manifest.read_text()), stale)

    def test_failed_companion_publication_restores_the_map(self):
        _, data, _ = fixture()
        terrain = SimpleNamespace(heights=np.zeros((2, 2)), x_axis=np.arange(2),
                                  y_axis=np.arange(2), datum=0)
        with tempfile.TemporaryDirectory() as tmp:
            root = Path(tmp)
            town, manifest = root / "town.glb", root / "trees.json"
            town.write_bytes(data)
            original_manifest = json.dumps({"map": {"map_sha256": hashlib.sha256(data).hexdigest()}}).encode()
            manifest.write_bytes(original_manifest)
            replace = Path.replace

            def fail_companion(source, target):
                if target == manifest:
                    raise OSError("simulated companion publication failure")
                return replace(source, target)

            with patch("src.road_scene_update.load_tree_map", return_value=(terrain, {}, {})), \
                    patch("src.road_scene_update.inspect_glb", return_value={"valid": True}), \
                    patch.object(Path, "replace", fail_companion):
                with self.assertRaisesRegex(OSError, "publication failure"):
                    update_road_offset(town, {"roads": {"offset_m": .02}}, .25, [manifest])
            self.assertEqual(town.read_bytes(), data)
            self.assertEqual(manifest.read_bytes(), original_manifest)
            self.assertFalse((root / "report.json").exists())
            self.assertFalse(list(root.glob(".road-offset-*")))


if __name__ == "__main__":
    unittest.main()
