import importlib.util
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import shapely


SPEC = importlib.util.spec_from_file_location(
    "bake_zombie_navmesh", Path(__file__).resolve().parents[1]/"tools/bake_zombie_navmesh.py")
baker = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(baker)


class NavmeshBakeTests(unittest.TestCase):
    def scene(self, obstacles=(), bounds=(0, 0, 20, 20)):
        return {"obstacles": list(obstacles), "raster": {
            "minimum": list(bounds[:2]), "maximum": list(bounds[2:])}}

    def test_free_tiles_share_portals_and_cover_rectangle(self):
        vertices, triangles, neighbors, bounds, meta = baker.bake(self.scene(), tile_size=7)
        self.assertEqual(meta["components"], 1)
        self.assertAlmostEqual(meta["triangle_area_m2"], 400)
        self.assertEqual(meta["tiles"], 9)
        self.assertTrue(np.all(vertices[:, 0] >= bounds[0]))
        self.assertTrue(np.all(vertices[:, 1] <= bounds[3]))
        self.assertEqual(len(triangles), len(neighbors))

    def test_rotated_obstacle_and_trunk_clearance_are_holes(self):
        s = 2**-.5
        obstacles = [[0, 10, 10, s, s, 2, 1], [1, 5, 6, 1, 0, .15, .15]]
        vertices, triangles, neighbors, _, meta = baker.bake(self.scene(obstacles), tile_size=6)
        expanded = baker.obstacle_polygons(obstacles)
        self.assertEqual(meta["components"], 1)
        union = shapely.union_all(expanded)
        expected = 400-float(shapely.area(union))
        self.assertAlmostEqual(meta["triangle_area_m2"], expected, places=5)
        # Check independently against body-only padding. The additional numeric
        # margin must keep every output triangle clear of the real body bounds.
        body = shapely.union_all(baker.obstacle_polygons(obstacles, numeric_margin=0))
        mesh_faces = shapely.polygons(vertices[triangles])
        self.assertFalse(np.any(shapely.intersects(mesh_faces, body)))
        self.assertTrue(np.any(neighbors == baker.NO_TRIANGLE))

    def test_fractional_map_boundary_is_retained_at_tile_corners(self):
        bounds = (-3001.459228515625, -3000.734619140625,
                  -2981.459228515625, -2980.734619140625)
        vertices, _, _, _, meta = baker.bake(self.scene(bounds=bounds), tile_size=7)
        self.assertEqual(meta["components"], 1)
        self.assertEqual(float(np.min(vertices[:, 0])), bounds[0])
        self.assertEqual(float(np.min(vertices[:, 1])), bounds[1])
        self.assertAlmostEqual(meta["triangle_area_m2"], 400)

    def test_fence_divides_components_without_false_portals(self):
        obstacles = [[2, 10, 10, 0, 1, 20, .01]]
        _, _, _, _, meta = baker.bake(self.scene(obstacles), tile_size=5)
        self.assertEqual(meta["components"], 2)

    def test_t_junction_is_subdivided_before_adjacency(self):
        # One edge on the left meets two independently triangulated intervals
        # on the right. They must become true matching shared mesh edges.
        coordinates = np.array([
            [[-10, 0], [0, 0], [0, 10]],
            [[-10, 0], [0, 10], [-10, 10]],
            [[0, 0], [10, 0], [0, 5]],
            [[10, 0], [10, 10], [0, 5]],
            [[0, 5], [10, 10], [0, 10]],
        ], dtype=np.float64)
        vertices, triangles, meta = baker.conform_tile_seams(coordinates, np.array([0.]), np.array([]))
        neighbors = baker.build_neighbors(triangles)
        self.assertEqual(meta["seam_split_triangles"], 1)
        self.assertEqual(baker.count_components(neighbors), 1)
        baker.validate_boundaries(vertices, triangles, neighbors, np.empty(0, dtype=object), (-10, 0, 10, 10))

    def test_mismatched_seam_is_detected(self):
        vertices = np.array([[0, 0], [10, 0], [0, 10], [10, 10]], dtype=np.float64)
        triangles = np.array([[0, 1, 2], [1, 3, 2]], dtype=np.uint32)
        neighbors = np.full((2, 3), baker.NO_TRIANGLE, dtype=np.uint32)
        with self.assertRaisesRegex(ValueError, "tile crack"):
            baker.validate_boundaries(vertices, triangles, neighbors, np.empty(0, dtype=object), (0, 0, 10, 10))

    def test_two_subdivided_edges_use_an_interior_centroid_fan(self):
        coordinates = np.array([
            [[0, 0], [10, 0], [0, 10]],
            [[0, 0], [0, 5], [-10, 0]],
            [[0, 5], [0, 10], [-10, 0]],
            [[0, 0], [-10, -10], [5, 0]],
            [[5, 0], [-10, -10], [10, 0]],
        ], dtype=np.float64)
        vertices, triangles, meta = baker.conform_tile_seams(coordinates, np.array([0.]), np.array([0.]))
        self.assertEqual(meta["seam_split_triangles"], 1)
        self.assertEqual(len(triangles), 9)
        self.assertEqual(baker.count_components(baker.build_neighbors(triangles)), 1)

    def test_double_precision_interior_vertices_are_preserved(self):
        coordinates = np.array([[[0., 0.], [1e-10, 0.], [0., 1.]]])
        vertices, triangles, _ = baker.conform_tile_seams(coordinates, np.array([]), np.array([]))
        self.assertEqual(len(vertices), 3)
        self.assertEqual(len(triangles), 1)
        self.assertTrue(np.any(vertices[:, 0] == 1e-10))

    def test_versioned_binary_layout_and_atomic_publication(self):
        vertices, triangles, neighbors, bounds, _ = baker.bake(self.scene(), tile_size=50)
        with tempfile.TemporaryDirectory() as directory:
            filename = Path(directory)/"test.navmesh"
            baker.write_mesh(filename, vertices, triangles, neighbors, bounds, "12"*32)
            data = filename.read_bytes()
            header = baker.HEADER.unpack(data[:128])
            self.assertEqual(header[0], b"GDNAV01\0")
            self.assertEqual(header[1:3], (len(vertices), len(triangles)))
            self.assertEqual(header[-1], bytes.fromhex("12"*32))
            self.assertEqual(len(data), 128+16*len(vertices)+24*len(triangles))
            self.assertEqual(struct.unpack_from("<dd", data, 128), tuple(vertices[0]))
            self.assertFalse(filename.with_name(filename.name+".tmp").exists())

    def test_invalid_obstacle_does_not_publish_geometry(self):
        with self.assertRaisesRegex(ValueError, "axes"):
            baker.bake(self.scene([[1, 5, 5, 0, 0, 1, 1]]))
        with self.assertRaisesRegex(ValueError, "tile size"):
            baker.bake(self.scene(), tile_size=0)

    def test_fully_blocked_scene_preserves_previously_published_mesh(self):
        vertices, triangles, neighbors, bounds, _ = baker.bake(self.scene(), tile_size=50)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory)/"world.navmesh"
            baker.write_mesh(output, vertices, triangles, neighbors, bounds, "12"*32)
            previous = output.read_bytes()
            source = Path(directory)/"blocked.json"
            source.write_text(json.dumps(self.scene([[0, 10, 10, 1, 0, 20, 20]])))
            with patch("sys.argv", ["bake_zombie_navmesh.py", "--scene", str(source),
                                    "--output", str(output), "--tile-size", "7"]):
                with self.assertRaisesRegex(ValueError, "no walkable triangles"):
                    baker.main()
            self.assertEqual(output.read_bytes(), previous)
            self.assertFalse(output.with_name(output.name+".tmp").exists())
            # Direct callers receive the same protection before any file opens.
            with self.assertRaisesRegex(ValueError, "nonempty"):
                baker.write_mesh(output, np.empty((0, 2)), np.empty((0, 3)),
                                 np.empty((0, 3)), bounds, "12"*32)
            self.assertEqual(output.read_bytes(), previous)
            self.assertFalse(output.with_name(output.name+".tmp").exists())


if __name__ == "__main__":
    unittest.main()
