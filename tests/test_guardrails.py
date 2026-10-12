"""Guardrail geometry, texture, clipping and atomic publication regressions."""
import hashlib
import io
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
from pyproj import Transformer
from shapely.geometry import LineString, box
import trimesh

from src.config import coordinate_frame, load_config
from src.glb import read_glb
from src.guardrails import add_guardrails, create_guardrail_texture, TEXTURE_PATH
from src.guardrail_scene import export_guardrail_scene, guardrail_query, prepare_guardrails, validate_guardrail_glb
from src.osm import parse_guardrails
from src.terrain import Terrain


def feature(points, id="way/1", tags=None):
    return {"id": id, "geometry": LineString(points), "tags": {"barrier": "guard_rail", **(tags or {})}}


class GuardrailTests(unittest.TestCase):
    def test_many_textured_posts_still_use_one_quad_and_shared_material(self):
        scene = trimesh.Scene()
        stats = add_guardrails(scene, [feature([(1, 1), (50, 1), (100, 1)]),
                                      feature([(1, 3), (100, 3)], "way/2")], lambda x, y: 5.)
        self.assertEqual(stats["triangles"], 4)
        self.assertEqual(stats["batches"], 1)
        mesh = next(iter(scene.geometry.values()))
        self.assertEqual(len(mesh.vertices), 8)
        self.assertAlmostEqual(mesh.bounds[1, 2], 5.75)
        np.testing.assert_allclose(mesh.visual.uv[:4, 0], [0, 0, 49.5, 49.5])
        document, _ = read_glb(scene.export(file_type="glb"))
        self.assertEqual(len(document["materials"]), 1)
        self.assertEqual(len(document["textures"]), 1)

    def test_texture_has_post_at_repeat_seam_and_clear_gap_below_rail(self):
        with Image.open(TEXTURE_PATH) as image:
            texture = np.asarray(image)
        np.testing.assert_array_equal(texture, np.asarray(create_guardrail_texture()))
        self.assertTrue(np.all(texture[:, [0, -1], 3] == 255))
        self.assertEqual(texture[100, 256, 3], 0)
        self.assertEqual(texture[20, 256, 3], 255)
        self.assertTrue(np.all(texture[:, :, 0] == texture[:, :, 1]))
        self.assertTrue(np.all(texture[:, :, 1] == texture[:, :, 2]))

    def test_cell_split_keeps_uv_phase_and_no_duplicate_boundary_line(self):
        scene = trimesh.Scene()
        stats = add_guardrails(scene, [feature([(-10, 128), (140, 128)])], lambda x, y: 0.)
        self.assertEqual(stats["batches"], 3)
        self.assertEqual(stats["triangles"], 6)
        self.assertAlmostEqual(stats["length_m"], 150)
        self.assertEqual(sorted(scene.geometry), ["guardrails_-1_1", "guardrails_0_1", "guardrails_1_1"])
        left = scene.geometry["guardrails_-1_1"].visual.uv[-2:, 0]
        right = scene.geometry["guardrails_0_1"].visual.uv[:2, 0]
        np.testing.assert_array_equal(left, right)

    def test_bends_and_terrain_ridge_keep_minimal_valid_spans(self):
        terrain = Terrain(np.array([[0., 4., 0.], [0., 4., 0.]]), (0, 0, 10, 10), datum=200)
        scene = trimesh.Scene()
        stats = add_guardrails(scene, [feature([(1, 5), (9, 5)])], terrain)
        self.assertEqual(stats["triangles"], 4)
        mesh = next(iter(scene.geometry.values()))
        for triangle in mesh.triangles:
            p = triangle.mean(axis=0)
            self.assertGreaterEqual(p[2], terrain(p[0], p[1]) - .030001)
        scene = trimesh.Scene()
        stats = add_guardrails(scene, [feature([(1, 1), (9, 1), (9, 5)], tags={"height": "3 ft"})], lambda x, y: 0)
        self.assertEqual(stats["triangles"], 4)
        self.assertAlmostEqual(next(iter(scene.geometry.values())).bounds[1, 2], .9144)

    def test_parse_only_guardrails_clip_and_retain_mapped_gap(self):
        def way(id, start, end, kind="guard_rail"):
            return {"type": "way", "id": id, "tags": {"barrier": kind},
                    "geometry": [{"lon": start, "lat": 1}, {"lon": end, "lat": 1}]}
        payload = {"elements": [way(1, -2, 4), way(2, 6, 12), way(3, 0, 10, "fence")]}
        lines = parse_guardrails(payload, Transformer.from_crs(4326, 4326, always_xy=True), (0, 0), box(0, 0, 10, 10))
        self.assertEqual(len(lines), 2)
        self.assertEqual([f["geometry"].length for f in lines], [4, 4])
        scene = trimesh.Scene()
        add_guardrails(scene, lines, lambda x, y: 0)
        mesh = next(iter(scene.geometry.values()))
        for triangle in mesh.triangles:
            self.assertFalse(triangle[:, 0].min() < 5 < triangle[:, 0].max())
        self.assertIn('way["barrier"="guard_rail"](47,19,48,20)', guardrail_query([19, 47, 20, 48]))

    def test_atomic_publication_frame_and_failed_candidate_preserves_last_layer(self):
        scratch = Path(__file__).resolve().parents[1] / "build"
        scratch.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=scratch) as temporary:
            root = Path(temporary)
            config_path = root / "config.json"
            config_path.write_text(json.dumps({"bbox": [19.35, 47.59, 19.36, 47.60]}))
            config = load_config(config_path)
            frame = coordinate_frame(config)
            map_path = root / "town.glb"
            map_path.write_bytes(b"published town bytes")
            digest = hashlib.sha256(map_path.read_bytes()).hexdigest()
            payload = root / "query.json"
            payload.write_text(json.dumps({"elements": [{"type": "way", "id": 1,
                "tags": {"barrier": "guard_rail"}, "geometry": [
                    {"lon": 19.355, "lat": 47.595}, {"lon": 19.3552, "lat": 47.595}]}]}))
            terrain = lambda x, y: 10
            with patch("src.guardrail_scene.load_tree_map", return_value=(terrain, frame, {"map_sha256": digest})), \
                 patch("src.guardrail_scene._download_osm_query", return_value=(payload, {})):
                result = prepare_guardrails(config_path=config_path, map_path=map_path, root=root)
                output = Path(result["output"])
                data = output.read_bytes()
                metadata = validate_guardrail_glb(data, digest)
                self.assertEqual(metadata["guardrails"]["ways"], 1)
                loaded = trimesh.load_scene(io.BytesIO(data), file_type="glb", process=False)
                world = loaded.to_geometry()
                self.assertAlmostEqual(world.bounds[0, 1], 9.97, places=5)
                self.assertAlmostEqual(world.bounds[1, 1], 10.75, places=5)
                self.assertEqual(map_path.read_bytes(), b"published town bytes")
                with patch("src.guardrail_scene.validate_guardrail_glb", side_effect=ValueError("bad export")):
                    with self.assertRaisesRegex(ValueError, "bad export"):
                        prepare_guardrails(config_path=config_path, map_path=map_path, root=root)
                self.assertEqual(output.read_bytes(), data)
                with self.assertRaisesRegex(ValueError, "metadata"):
                    validate_guardrail_glb(data, "0" * 64)

    def test_empty_query_can_publish_an_empty_layer(self):
        scene = trimesh.Scene()
        stats = add_guardrails(scene, [], lambda x, y: 0)
        scene.metadata.update(schema="godollo.guardrails", version=1, map_sha256="0" * 64, guardrails=stats)
        metadata = validate_guardrail_glb(export_guardrail_scene(scene), "0" * 64)
        self.assertEqual(metadata["guardrails"]["ways"], 0)


if __name__ == "__main__":
    unittest.main()
