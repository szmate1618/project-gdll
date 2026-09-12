"""Offline checks that tree elevations follow the published GLB's triangles."""
import copy
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
import trimesh

from src.config import coordinate_frame, load_config
from src.terrain import Terrain
from src.tree_map import load_tree_map


class TreeMapTests(unittest.TestCase):
    def setUp(self):
        self.directory = tempfile.TemporaryDirectory()
        self.addCleanup(self.directory.cleanup)
        self.root = Path(self.directory.name)
        self.path = self.root / "map.glb"
        config_path = self.root / "config.json"
        config_path.write_text(json.dumps({"bbox": [19.355, 47.596, 19.357, 47.598], "crs": "EPSG:32634"}))
        self.config = load_config(config_path)
        self.frame = coordinate_frame(self.config)
        self.heights = np.array([[0.123456789, 0., 7.765432198, 1.],
                                 [0., 10.123456789, 0., 5.],
                                 [3., -4.987654321, 1., 8.]])
        self.rotation = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.],
                                  [0., -1., 0., 0.], [0., 0., 0., 1.]])

    def write_map(self, *, heights=None, transform=None, modify_mesh=None, modify_metadata=None):
        heights = self.heights if heights is None else heights
        terrain = Terrain(heights, self.frame["bounds"], datum=211.125)
        mesh = terrain.mesh(Image.new("RGB", (2, 2), (30, 60, 90)))
        if modify_mesh:
            modify_mesh(mesh)
        scene = trimesh.Scene(base_frame="local_meters")
        scene.add_geometry(mesh, geom_name="terrain", node_name="terrain")
        scene.graph.update(frame_from="world", frame_to="local_meters",
                           matrix=self.rotation if transform is None else transform)
        scene.graph.base_frame = "world"
        scene.metadata.update({
            "bbox_wgs84": self.config["bbox"], "crs": self.config["crs"],
            "origin_wgs84": list(self.frame["center"]), "origin_projected_m": list(self.frame["origin"]),
            "origin_elevation_m": terrain.datum, "units": "meters",
            "gltf_axes": {"X": "projected east", "Y": "up", "Z": "projected south"},
            "terrain": {"grid_vertices": [terrain.cols, terrain.rows]},
        })
        if modify_metadata:
            modify_metadata(scene.metadata)
        self.path.write_bytes(scene.export(file_type="glb"))
        return mesh

    def test_reads_published_float32_vertices_axes_and_triangle_planes(self):
        mesh = self.write_map()
        # A map in an otherwise empty directory is sufficient; no processed
        # terrain cache, downloaded rasters, or full scene loader is consulted.
        with patch("trimesh.load_scene", side_effect=AssertionError("Full scene loading is unnecessary")):
            terrain, frame, metadata = load_tree_map(self.path, self.config)
        self.assertIsInstance(terrain, Terrain)
        exported = mesh.vertices.astype(np.float32).astype(float).reshape(3, 4, 3)
        np.testing.assert_array_equal(terrain.heights, self.heights.astype(np.float32).astype(float))
        self.assertNotEqual(terrain.heights[0, 0], self.heights[0, 0])
        self.assertEqual(terrain.datum, 211.125)
        np.testing.assert_array_equal(terrain(exported[:, :, 0], exported[:, :, 1]), exported[:, :, 2])
        self.assertEqual(tuple(terrain.bounds), (exported[0, 0, 0], exported[-1, 0, 1],
                                               exported[0, -1, 0], exported[0, 0, 1]))
        # Barycentric samples on both sides of a saddle's NW-SE diagonal must
        # match exported triangles, rather than bilinear or pre-export heights.
        for vertex_ids, weights in (((0, 4, 5), (.2, .5, .3)), ((0, 5, 1), (.2, .3, .5))):
            with self.subTest(vertices=vertex_ids):
                point = np.asarray(weights) @ exported.reshape(-1, 3)[list(vertex_ids)]
                self.assertAlmostEqual(terrain(point[0], point[1]), point[2], places=12)
                world = self.rotation @ np.r_[point, 1.]
                self.assertEqual(world[0], point[0])
                self.assertEqual(world[1], point[2])
                self.assertEqual(world[2], -point[1])
        self.assertEqual(frame["origin"], self.frame["origin"])
        self.assertEqual(metadata["map_sha256"], hashlib.sha256(self.path.read_bytes()).hexdigest())
        self.assertEqual(metadata["terrain_grid_vertices"], [4, 3])
        self.assertEqual(metadata["origin_projected_m"], list(self.frame["origin"]))
        self.assertEqual(metadata["bbox_wgs84"], self.config["bbox"])

    def test_rejects_config_from_another_map(self):
        self.write_map()
        for key, value in (("bbox", [19.356, 47.596, 19.358, 47.598]), ("crs", "EPSG:32633")):
            with self.subTest(field=key):
                config = copy.deepcopy(self.config)
                config[key] = value
                with self.assertRaisesRegex(ValueError, "does not match the current config"):
                    load_tree_map(self.path, config)

    def test_rejects_inconsistent_coordinate_metadata(self):
        for key, value in (("origin_projected_m", [0., 0.]), ("origin_wgs84", [0., 0.]),
                           ("units", "feet"), ("origin_elevation_m", float("nan"))):
            with self.subTest(field=key):
                self.write_map(modify_metadata=lambda metadata: metadata.update({key: value}))
                with self.assertRaises(ValueError):
                    load_tree_map(self.path, self.config)

    def test_rejects_terrain_with_unexpected_world_transform(self):
        transform = self.rotation.copy()
        transform[1, 3] = 10.
        self.write_map(transform=transform)
        with self.assertRaisesRegex(ValueError, "terrain transform"):
            load_tree_map(self.path, self.config)

    def test_rejects_grid_vertices_that_do_not_match_map_bounds(self):
        def move_vertex(mesh):
            mesh.vertices[1, 0] += 0.25
        self.write_map(modify_mesh=move_vertex)
        with self.assertRaisesRegex(ValueError, "terrain grid"):
            load_tree_map(self.path, self.config)

    def test_rejects_a_different_grid_diagonal(self):
        def change_diagonal(mesh):
            mesh.faces[0] = [0, 4, 1]
        self.write_map(modify_mesh=change_diagonal)
        with self.assertRaisesRegex(ValueError, "terrain topology"):
            load_tree_map(self.path, self.config)

    def test_rejects_truncated_glb(self):
        self.write_map()
        self.path.write_bytes(self.path.read_bytes()[:-4])
        with self.assertRaisesRegex(ValueError, "complete GLB"):
            load_tree_map(self.path, self.config)

    def test_rejects_accessor_outside_embedded_buffer(self):
        self.write_map()
        data = self.path.read_bytes()
        length, _ = struct.unpack_from("<I4s", data, 12)
        document = json.loads(data[20:20 + length])
        position_index = document["meshes"][0]["primitives"][0]["attributes"]["POSITION"]
        document["accessors"][position_index]["byteOffset"] = len(data)
        encoded = json.dumps(document).encode("utf-8")
        encoded += b" " * (-len(encoded) % 4)
        body = struct.pack("<I4s", len(encoded), b"JSON") + encoded + data[20 + length:]
        self.path.write_bytes(struct.pack("<4sII", b"glTF", 2, 12 + len(body)) + body)
        with self.assertRaisesRegex(ValueError, "accessor extends outside"):
            load_tree_map(self.path, self.config)

    def test_fingerprint_changes_when_published_elevations_change(self):
        self.write_map()
        old_terrain, _, old_metadata = load_tree_map(self.path, self.config)
        self.write_map(heights=self.heights + 5.)
        new_terrain, _, new_metadata = load_tree_map(self.path, self.config)
        self.assertNotEqual(new_metadata["map_sha256"], old_metadata["map_sha256"])
        self.assertAlmostEqual(new_terrain(0., 0.) - old_terrain(0., 0.), 5., places=6)


if __name__ == "__main__":
    unittest.main()
