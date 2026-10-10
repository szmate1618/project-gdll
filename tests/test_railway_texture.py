"""Metric artwork, curved UVs, and safe texture-only map publication."""
import copy
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
from pyproj import Transformer
from shapely.geometry import LineString
import trimesh
from trimesh.visual.material import PBRMaterial
from trimesh.visual.texture import TextureVisuals

from src.config import coordinate_frame, load_config
from src.osm import parse_railways
from src.railways import add_railways
from src.railway_texture import TEXTURE_PATH, configure_track_sampler, track_phases, track_uv
from src.railway_texture_apply import _read_glb, apply_railway_texture, prepare_texture_patch
from src.terrain import Terrain


class TrackTextureTests(unittest.TestCase):
    def test_artwork_has_1435_mm_clear_gauge_and_240_mm_tie_width(self):
        image = np.asarray(Image.open(TEXTURE_PATH))
        self.assertEqual(image.shape, (240, 1600, 3))
        columns = image.mean(axis=0)
        steel = np.flatnonzero(columns[:, 0] > 155)
        runs = np.split(steel, np.flatnonzero(np.diff(steel) > 1) + 1)
        self.assertEqual(len(runs), 2)
        self.assertEqual(runs[1][0] - (runs[0][-1] + 1), 574)
        self.assertAlmostEqual(574 / 400, 1.435)
        self.assertTrue(all(len(run) == 28 for run in runs))
        # Wood grain varies in brightness; its warm color identifies the tie
        # independently of a light grain stripe or a dark gravel stone.
        rows = image[:, 550:1050].mean(axis=1)
        tie = np.flatnonzero(rows[:, 0] - rows[:, 2] > 10)
        np.testing.assert_array_equal(tie, np.arange(72, 168))
        self.assertAlmostEqual(len(tie) / 400, 0.24)

    def test_tie_centers_repeat_every_60_cm_along_vertical_track(self):
        line = LineString([(10, 20), (10, 25)])
        stations = np.arange(0.3, 4.9, 0.6)
        vertices = np.column_stack((np.full(len(stations), 10), 20 + stations, np.zeros(len(stations))))
        uv = track_uv(vertices, line)
        np.testing.assert_allclose(uv[:, 0], 0.5)
        np.testing.assert_allclose(uv[:, 1] % 1, 0.5, atol=1e-12)
        edges = track_uv([[10 - 0.7175, 22, 0], [10 + 0.7175, 22, 0]], line)
        self.assertAlmostEqual(abs(np.diff(edges[:, 0])[0]) * 4, 1.435)

    def test_bends_use_centerline_station_and_joined_reversed_ways_share_phase(self):
        bent = LineString([(0, 0), (4, 0), (4, 3)])
        uv = track_uv([[1, 0, 0], [4, 1, 0], [4, 3, 0]], bent)
        np.testing.assert_allclose(uv[:, 0], 0.5)
        np.testing.assert_allclose(uv[:, 1], np.array([1, 5, 7]) / 0.6)
        lines = [LineString([(0, 0), (1.1, 0)]), LineString([(2.3, 0), (1.1, 0)])]
        phases = track_phases(lines)
        joins = [track_uv([[1.1, 0, 0]], line, phase)[0, 1] % 1 for line, phase in zip(lines, phases)]
        self.assertAlmostEqual(joins[0], joins[1])

    def test_width_tags_do_not_scale_the_rail_gauge_and_sampler_repeats_only_along_track(self):
        scene = trimesh.Scene()
        add_railways(scene, [{"id": "way/1", "geometry": LineString([(0, 0), (20, 0)]),
                              "tags": {"railway": "rail", "width": "8"}}], lambda x, y: 0, {})
        mesh = scene.geometry["railways_ballast"]
        np.testing.assert_allclose(mesh.visual.uv[:, 0], 0.5 + mesh.vertices[:, 1] / 4)
        document, _ = _read_glb(scene.export(file_type="glb", tree_postprocessor=configure_track_sampler))
        material = next(m for m in document["materials"] if m["name"] == "railway_ballast")
        texture = document["textures"][material["pbrMetallicRoughness"]["baseColorTexture"]["index"]]
        sampler = document["samplers"][texture["sampler"]]
        self.assertEqual((sampler["wrapS"], sampler["wrapT"]), (33071, 10497))
        self.assertIn("TEXCOORD_0", document["meshes"][0]["primitives"][0]["attributes"])


class TexturePublicationTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="railway-texture-test-", dir="/tmp")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        config_path = self.root / "config.json"
        config_path.write_text(json.dumps({"bbox": [19.355, 47.596, 19.357, 47.598], "crs": "EPSG:32634"}))
        self.config = load_config(config_path)
        frame = coordinate_frame(self.config)
        inverse = Transformer.from_crs(self.config["crs"], "EPSG:4326", always_xy=True)
        geometry = []
        for x, y in [(-20, 0), (20, 0)]:
            lon, lat = inverse.transform(frame["origin"][0] + x, frame["origin"][1] + y)
            geometry.append({"lon": lon, "lat": lat})
        payload = {"elements": [{"type": "way", "id": 1, "tags": {"railway": "rail"}, "geometry": geometry}]}
        osm_path = self.root / "osm.json"
        osm_path.write_text(json.dumps(payload))
        terrain = Terrain(np.zeros((3, 3)), frame["bounds"])
        scene = trimesh.Scene(base_frame="local_meters")
        scene.add_geometry(terrain.mesh(Image.new("RGB", (2, 2), (35, 60, 30))), geom_name="terrain")
        features = parse_railways(payload, frame["transformer"], frame["origin"], frame["clip"])
        add_railways(scene, features, terrain, {**self.config, "_clip_polygon": frame["clip"]})
        self.generated = scene.geometry["railways_ballast"].copy()
        scene.geometry["railways_ballast"].visual = TextureVisuals(material=PBRMaterial(
            name="railway_ballast", baseColorFactor=[107, 111, 116, 255], doubleSided=True))
        scene.graph.update(frame_from="world", frame_to="local_meters",
                           matrix=np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]]))
        scene.graph.base_frame = "world"
        scene.metadata.update({"bbox_wgs84": self.config["bbox"], "crs": self.config["crs"],
            "origin_wgs84": list(frame["center"]), "origin_projected_m": list(frame["origin"]),
            "origin_elevation_m": terrain.datum, "units": "meters",
            "gltf_axes": {"X": "projected east", "Y": "up", "Z": "projected south"},
            "terrain": {"grid_vertices": [3, 3]}, "sources": {"osm": {"path": str(osm_path)}}})
        self.map = self.root / "godollo.glb"
        self.original = scene.export(file_type="glb")
        self.map.write_bytes(self.original)
        self.tree = self.root / "trees.instances.json"
        self.manifest = {"map": {"path": self.map.name, "map_sha256": hashlib.sha256(self.original).hexdigest()},
                         "instances": [{"transform": np.eye(4).T.ravel().tolist()}], "assets": {"tree": {"sha256": "preserved"}}}
        self.tree.write_text(json.dumps(self.manifest))
        self.report = self.root / "report.json"
        self.report.write_text(json.dumps({"railways": {"railway_segments": 1}}))

    def test_patch_retains_original_binary_geometry_nodes_and_materials(self):
        before, binary = _read_glb(self.original)
        after, modified = _read_glb(prepare_texture_patch(self.original, self.generated))
        self.assertEqual(modified[:len(binary)], binary)
        self.assertEqual(after["nodes"], before["nodes"])
        self.assertEqual(after["materials"][:len(before["materials"])], before["materials"])
        for a, b in zip(after["meshes"], before["meshes"]):
            for new, old in zip(a["primitives"], b["primitives"]):
                self.assertEqual(new["attributes"]["POSITION"], old["attributes"]["POSITION"])
                self.assertEqual(new["indices"], old["indices"])
        self.assertEqual(len(after["images"]), len(before["images"]) + 1)

    def test_publication_backs_up_map_and_preserves_tree_data_while_refreshing_hash(self):
        result = apply_railway_texture(self.map, self.config)
        self.assertEqual((Path(result["backup"]) / self.map.name).read_bytes(), self.original)
        tree = json.loads(self.tree.read_text())
        expected = copy.deepcopy(self.manifest)
        expected["map"]["map_sha256"] = hashlib.sha256(self.map.read_bytes()).hexdigest()
        self.assertEqual(tree, expected)
        self.assertTrue(result["tree_fingerprint_refreshed"])
        report = json.loads(self.report.read_text())
        self.assertTrue(report["verification"]["valid"])
        self.assertEqual(report["verification"]["embedded_images"], 2)

    def test_stale_manifest_and_failed_validation_preserve_published_outputs(self):
        self.manifest["map"]["map_sha256"] = "0" * 64
        self.tree.write_text(json.dumps(self.manifest))
        with self.assertRaisesRegex(ValueError, "Tree manifest does not match"):
            apply_railway_texture(self.map, self.config)
        self.assertEqual(self.map.read_bytes(), self.original)
        self.manifest["map"]["map_sha256"] = hashlib.sha256(self.original).hexdigest()
        self.tree.write_text(json.dumps(self.manifest))
        companions = [self.tree.read_bytes(), self.report.read_bytes()]
        with patch("src.railway_texture_apply.inspect_glb", side_effect=ValueError("invalid candidate")):
            with self.assertRaisesRegex(ValueError, "invalid candidate"):
                apply_railway_texture(self.map, self.config)
        self.assertEqual(self.map.read_bytes(), self.original)
        self.assertEqual([self.tree.read_bytes(), self.report.read_bytes()], companions)
        self.assertFalse(list(self.root.glob(".railway-texture-*")))

    def test_mismatched_geometry_is_rejected_before_publication(self):
        self.generated.vertices[0, 0] += 0.01
        with self.assertRaisesRegex(ValueError, "differs from the published map"):
            prepare_texture_patch(self.original, self.generated)


if __name__ == "__main__":
    unittest.main()
