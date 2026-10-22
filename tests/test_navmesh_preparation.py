"""Offline map-to-navigation preparation with small exported scene fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
import shapely
import trimesh

from src import navmesh_preparation
from tools.bake_zombie_navmesh import HEADER


def terrain():
    return trimesh.Trimesh(vertices=[[-20, 2, -20], [20, 2, -20],
                                      [20, 2, 20], [-20, 2, 20]],
                           faces=[[0, 2, 1], [0, 3, 2]], process=False)


def panel(start, end):
    return trimesh.Trimesh(vertices=[[start, 2, 0], [end, 2, 0],
                                      [end, 4, 0], [start, 4, 0]],
                           faces=[[0, 1, 2], [0, 2, 3]], process=False)


def walkable_geometry(path):
    data = path.read_bytes()
    header = HEADER.unpack_from(data)
    vertices = np.frombuffer(data, dtype="<f8", count=header[1] * 2,
                             offset=HEADER.size).reshape(-1, 2)
    cursor = HEADER.size + header[11]
    polygons = []
    for _ in range(header[2]):
        count, = struct.unpack_from("<I", data, cursor)
        indices = np.frombuffer(data, dtype="<u4", count=count, offset=cursor + 4)
        polygons.append(shapely.Polygon(vertices[indices]))
        cursor += 4 + count * 8
    if cursor != len(data):
        raise AssertionError("Unexpected serialized polygon payload")
    return shapely.union_all(polygons), polygons, header


class NavmeshPreparationTests(unittest.TestCase):
    def setUp(self):
        directory = tempfile.TemporaryDirectory(prefix="navmesh-preparation-", dir="/tmp")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        self.map = self.root / "town.pending.glb"
        self.output = self.root / "town.navmesh"

    def prepare(self, scene):
        self.map.write_bytes(scene.export(file_type="glb"))
        report = navmesh_preparation.prepare_navmesh(self.map, self.output)
        source = json.loads(self.output.with_suffix(".navigation.json").read_text())
        return report, source

    def test_exported_world_transforms_and_building_roof_are_applied_once(self):
        scene = trimesh.Scene(base_frame="local_meters")
        # Generator coordinates are east/north/up before the exported root turn.
        ground = trimesh.Trimesh(vertices=[[-20, -30, 17], [20, -30, 17],
                                            [20, 30, 17], [-20, 30, 17]],
                                 faces=[[0, 1, 2], [0, 2, 3]], process=False)
        scene.add_geometry(ground, geom_name="terrain", node_name="terrain")
        angle = np.pi / 6
        transform = trimesh.transformations.rotation_matrix(angle, [0, 0, 1])
        transform[:3, 3] = [3, -4, 20]
        scene.add_geometry(trimesh.creation.box(extents=[8, 4, 6]),
                           geom_name="building_test_walls", node_name="building_test_walls",
                           transform=transform)
        # Roof overhangs must be included in the conservative solid footprint.
        scene.add_geometry(trimesh.creation.box(extents=[10, 6, .2]),
                           geom_name="building_test_roof", node_name="building_test_roof",
                           transform=transform)
        scene.add_geometry(trimesh.creation.box(extents=[1000, 1000, 1000]),
                           geom_name="building_test_detailed_walls", node_name="building_test_detailed_walls")
        near = trimesh.creation.box(extents=[1000, 1000, 1000])
        near.metadata["building_detail"] = {"level": "near"}
        scene.add_geometry(near, geom_name="building_other_walls", node_name="building_other_walls")
        rotation = np.array([[1., 0., 0., 100.], [0., 0., 1., 50.],
                             [0., -1., 0., -100.], [0., 0., 0., 1.]])
        scene.graph.update(frame_from="world", frame_to="local_meters", matrix=rotation)
        scene.graph.base_frame = "world"
        report, source = self.prepare(scene)
        self.assertEqual(source["raster"], {"minimum": [80., -130.], "maximum": [120., -70.]})
        self.assertEqual(report["obstacle_counts"], {"buildings": 1, "fences": 0, "trees": 0})
        obstacle, = source["obstacles"]
        np.testing.assert_allclose(obstacle[1:3], [103., -96.], atol=1e-5)
        self.assertLess(4 * obstacle[5] * obstacle[6], 61)
        u = np.array(obstacle[3:5])
        v = np.array([-u[1], u[0]])
        c = np.array(obstacle[1:3])
        footprint = shapely.Polygon([c-u*obstacle[5]-v*obstacle[6], c+u*obstacle[5]-v*obstacle[6],
                                     c+u*obstacle[5]+v*obstacle[6], c-u*obstacle[5]+v*obstacle[6]])
        corners = np.array([[-5, -3, 0], [5, -3, 0], [5, 3, 0], [-5, 3, 0]])
        expected = trimesh.transform_points(trimesh.transform_points(corners, transform), rotation)[:, [0, 2]]
        self.assertTrue(shapely.covers(footprint, shapely.multipoints(expected)))

    def test_fence_material_batch_keeps_gate_and_excludes_trees(self):
        scene = trimesh.Scene()
        scene.add_geometry(terrain(), geom_name="terrain", node_name="terrain")
        scene.add_geometry(trimesh.util.concatenate([panel(-20, -2), panel(2, 20)]),
                           geom_name="fences_wood", node_name="fences_wood")
        # A large tree mesh must never become a baked obstacle.
        scene.add_geometry(trimesh.creation.box(extents=[30, 30, 30]),
                           geom_name="tree_1", node_name="tree_1")
        report, source = self.prepare(scene)
        self.assertEqual(source["obstacle_counts"], {"buildings": 0, "fences": 2, "trees": 0})
        self.assertTrue(all(record[0] == 2 for record in source["obstacles"]))
        walkable, _, _ = walkable_geometry(self.output)
        self.assertTrue(shapely.covers(walkable, shapely.LineString([[0, -10], [0, 10]])))
        self.assertFalse(shapely.covers(walkable, shapely.LineString([[10, -10], [10, 10]])))
        self.assertEqual(report["components"], 1)

    def test_header_sidecar_and_report_record_exact_map_and_source_hashes(self):
        scene = trimesh.Scene()
        scene.add_geometry(terrain(), geom_name="terrain", node_name="terrain")
        report, source = self.prepare(scene)
        source_data = self.output.with_suffix(".navigation.json").read_bytes()
        source_hash = hashlib.sha256(source_data).hexdigest()
        map_hash = hashlib.sha256(self.map.read_bytes()).hexdigest()
        metadata = json.loads(self.output.with_name("town.navmesh.json").read_text())
        walkable, polygons, header = walkable_geometry(self.output)
        self.assertEqual(header[0], b"GDNAV02\0")
        self.assertEqual(header[-1].hex(), source_hash)
        self.assertEqual(report["source_sha256"], source_hash)
        self.assertEqual(metadata["source_replay_sha256"], source_hash)
        self.assertEqual(metadata["map_sha256"], map_hash)
        self.assertEqual(source["map_sha256"], map_hash)
        self.assertEqual(report["map_sha256"], map_hash)
        self.assertEqual(source["model"], "town.glb")
        self.assertEqual(source["model_bytes"], self.map.stat().st_size)
        self.assertEqual(metadata["output_sha256"], hashlib.sha256(self.output.read_bytes()).hexdigest())
        self.assertEqual(metadata["validation"]["components"], 1)
        self.assertTrue(all(3 <= len(p.exterior.coords)-1 <= 4 for p in polygons))
        self.assertAlmostEqual(shapely.area(walkable), 1600)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()),
                         ["town.navigation.json", "town.navmesh", "town.navmesh.json", "town.pending.glb"])

    def test_missing_terrain_rejects_before_replacing_existing_outputs(self):
        scene = trimesh.Scene()
        scene.add_geometry(trimesh.creation.box(), geom_name="building_test_walls", node_name="building_test_walls")
        paths = [self.output, self.output.with_suffix(".navigation.json"),
                 self.output.with_name("town.navmesh.json")]
        for path in paths:
            path.write_bytes(b"last good publication")
        self.map.write_bytes(scene.export(file_type="glb"))
        with self.assertRaisesRegex(ValueError, "terrain"):
            navmesh_preparation.prepare_navmesh(self.map, self.output)
        for path in paths:
            self.assertEqual(path.read_bytes(), b"last good publication")

    def test_failed_merge_validation_keeps_all_existing_outputs(self):
        scene = trimesh.Scene()
        scene.add_geometry(terrain(), geom_name="terrain", node_name="terrain")
        self.map.write_bytes(scene.export(file_type="glb"))
        paths = [self.output, self.output.with_suffix(".navigation.json"),
                 self.output.with_name("town.navmesh.json")]
        for path in paths:
            path.write_bytes(b"last good publication")
        with patch.object(navmesh_preparation, "merge_file", side_effect=ValueError("coverage differs")):
            with self.assertRaisesRegex(ValueError, "coverage differs"):
                navmesh_preparation.prepare_navmesh(self.map, self.output)
        for path in paths:
            self.assertEqual(path.read_bytes(), b"last good publication")
        self.assertFalse(any(path.name.startswith(".navmesh-") for path in self.root.iterdir()))


if __name__ == "__main__":
    unittest.main()
