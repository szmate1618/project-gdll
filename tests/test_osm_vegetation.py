"""OSM vegetation topology, map grounding and independent publication regressions."""
import copy
import hashlib
import json
from pathlib import Path
import unittest
from unittest.mock import patch

import numpy as np
from pyproj import Transformer
from shapely.geometry import Point, Polygon, box
from shapely.ops import unary_union

from src.osm_vegetation import parse_vegetation, vegetation_query
from src.osm_vegetation_placements import forest_patches, prepare_osm_vegetation, tree_instances
from src.tree_assets import load_impostor
from src.tree_map import load_tree_map
from tests import test_tree_map
from tools.create_tree_impostors import build_impostor
from tools.prepare_osm_vegetation import main


ROOT = Path(__file__).resolve().parents[1]


def way(identifier, points, **tags):
    return {"type": "way", "id": identifier, "tags": tags,
            "geometry": [{"lon": x, "lat": n} for x, n in points]}


class VegetationGeometryTests(unittest.TestCase):
    def setUp(self):
        self.frame = {"transformer": Transformer.from_crs(4326, 4326, always_xy=True),
                      "origin": (0, 0), "clip": box(-100, -100, 100, 100)}

    def parse(self, elements):
        return parse_vegetation({"elements": elements}, self.frame)

    def test_query_contains_nodes_rows_and_both_woodland_tags(self):
        query = vegetation_query([1, 2, 3, 4])
        for text in ('node["natural"="tree"](2,1,4,3)', 'way["natural"="tree_row"]',
                     'relation["type"="multipolygon"]["natural"="wood"]',
                     'relation["type"="multipolygon"]["landuse"="forest"]', 'out body geom'):
            self.assertIn(text, query)

    def test_relation_stitches_split_outer_keeps_clearing_and_island(self):
        elements = [way(1, [(0, 0), (20, 0), (20, 20)], landuse="forest"),
                    way(2, [(20, 20), (0, 20), (0, 0)], landuse="forest"),
                    way(3, [(5, 5), (15, 5), (15, 15), (5, 15), (5, 5)]),
                    way(4, [(8, 8), (12, 8), (12, 12), (8, 12), (8, 8)])]
        relation = {"type": "relation", "id": 10, "tags": {"type": "multipolygon", "natural": "wood"},
                    "members": [{"type": "way", "ref": i, "role": "inner" if i == 3 else "outer"}
                                for i in range(1, 5)]}
        trees, rows, forests = self.parse([*elements, relation])
        self.assertEqual((trees, rows), ([], []))
        self.assertEqual([f["id"] for f in forests], ["relation/10"])
        woodland = forests[0]["geometry"]
        self.assertAlmostEqual(woodland.area, 316)
        self.assertTrue(woodland.covers(Point(10, 10)))
        self.assertFalse(woodland.covers(Point(6, 6)))
        self.assertEqual(len(self.parse([relation, *reversed(elements)])[2]), 1)

    def test_missing_member_and_open_rings_are_rejected(self):
        with self.assertRaisesRegex(ValueError, "not closed"):
            self.parse([way(1, [(0, 0), (20, 0), (20, 20)], natural="wood")])
        relation = {"type": "relation", "id": 10, "tags": {"type": "multipolygon", "landuse": "forest"},
                    "members": [{"type": "way", "ref": 1, "role": "outer"},
                                {"type": "way", "ref": 2, "role": "inner"}]}
        outer = way(1, [(0, 0), (20, 0), (20, 20), (0, 20), (0, 0)])
        with self.assertRaisesRegex(ValueError, "missing way/2"):
            self.parse([outer, relation])
        inner = way(2, [(5, 5), (10, 5), (10, 10)])
        with self.assertRaisesRegex(ValueError, "lose a clearing"):
            self.parse([outer, inner, relation])
        inner = way(2, [(30, 30), (40, 30), (40, 40), (30, 40), (30, 30)])
        with self.assertRaisesRegex(ValueError, "clearing outside"):
            self.parse([outer, inner, relation])

    def test_api_node_references_clipping_and_park_exclusion(self):
        node = {"type": "node", "id": 1, "lat": 2, "lon": 1, "tags": {"natural": "tree"}}
        out = {**node, "id": 2, "lon": 200}
        row = {"type": "way", "id": 3, "tags": {"natural": "tree_row"}, "nodes": [1, 2]}
        park = way(4, [(0, 0), (10, 0), (10, 10), (0, 0)], leisure="park")
        trees, rows, forests = self.parse([node, out, row, park, node])
        self.assertEqual(len(trees), 1)
        self.assertEqual(list(trees[0]["geometry"].coords), [(1, 2)])
        self.assertEqual(list(rows[0]["geometry"].coords), [(1, 2), (100, 2)])
        self.assertEqual(forests, [])

    def test_subdivision_union_retains_holes_and_removes_overlap(self):
        shape = box(-200, -200, 900, 900).difference(box(50, 50, 150, 150))
        forests = [{"geometry": shape}, {"geometry": box(200, 200, 800, 800)}]
        patches = forest_patches(forests, (-1000, -1000, 1000, 1000))
        polygons = [Polygon(p["polygon"], p["holes"]) for p in patches]
        self.assertAlmostEqual(sum(p.area for p in polygons), 1200000)
        self.assertTrue(unary_union(polygons).equals(shape))
        for polygon in polygons:
            self.assertTrue(polygon.is_valid)
            self.assertTrue(polygon.exterior.is_ccw)
            self.assertTrue(all(not r.is_ccw for r in polygon.interiors))
            x0, n0, x1, n1 = polygon.bounds
            self.assertLessEqual(max(x1-x0, n1-n0), 500)
        self.assertEqual(patches, forest_patches(list(reversed(forests)), (-1000, -1000, 1000, 1000)))


class VegetationPublicationTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = {}
        for species, height in (("tree", 10.), ("pine", 14.)):
            path = ROOT / f"assets/{species}_impostor.glb"
            cls.assets[species] = path.read_bytes() if path.exists() else build_impostor(species, height, ROOT / "assets")

    def setUp(self):
        self.map = test_tree_map.TreeMapTests()
        self.map.setUp()
        self.addCleanup(self.map.doCleanups)
        self.map.write_map()
        self.root = self.map.root
        (self.root / "assets").mkdir()
        for kind, data in self.assets.items():
            (self.root / f"assets/{kind}_impostor.glb").write_bytes(data)
        inverse = Transformer.from_crs(self.map.config["crs"], 4326, always_xy=True)
        x, n = self.map.frame["origin"]
        self.lon, self.lat = inverse.transform(x + 10, n + 20)
        self.node = {"type": "node", "id": 9, "lon": self.lon, "lat": self.lat,
                     "tags": {"natural": "tree", "height": "18", "diameter_crown": "6", "leaf_type": "needleleaved"}}
        self.input = self.root / "osm.json"
        self.input.write_text(json.dumps({"elements": [self.node]}))
        self.output = self.root / "osm"

    def prepare(self):
        return prepare_osm_vegetation(config_path=self.root / "config.json", map_path=self.map.path,
                                      input_path=self.input, root=self.root)

    def test_grounded_matrix_asset_bounds_and_separate_outputs(self):
        original_map = self.map.path.read_bytes()
        defaults = {self.root / "forest_patches.json": b'preserved forest',
                    self.root / "trees.instances.json": b'preserved trees'}
        for path, value in defaults.items():
            path.write_bytes(value)
        result = self.prepare()
        self.assertEqual(result["mapped_nodes"], 1)
        doc = json.loads((self.output / "trees.instances.json").read_text())
        forest = json.loads((self.output / "forest_patches.json").read_text())
        self.assertEqual(forest["patches"], [])
        record = doc["instances"][0]
        terrain, _, metadata = load_tree_map(self.map.path, self.map.config)
        np.testing.assert_allclose(record["position"], [10, terrain(10, 20), -20], atol=1e-7)
        np.testing.assert_allclose(record["dimensions_m"], [6, 18, 6])
        matrix = np.array(record["matrix"]).reshape(4, 4, order="F")
        np.testing.assert_allclose(matrix @ [0, 0, 0, 1], [*record["position"], 1])
        self.assertEqual(record["asset"], "pine")
        self.assertEqual(doc["map"]["map_sha256"], metadata["map_sha256"])
        self.assertEqual(doc["source"]["sha256"], hashlib.sha256(self.input.read_bytes()).hexdigest())
        for kind, asset in doc["assets"].items():
            self.assertEqual((self.output / asset["path"]).read_bytes(), self.assets[kind])
        self.assertEqual(self.map.path.read_bytes(), original_map)
        for path, value in defaults.items():
            self.assertEqual(path.read_bytes(), value)

    def test_rows_avoid_duplicate_nodes_and_default_unknown_dimensions(self):
        assets = {kind: load_impostor(self.root / f"assets/{kind}_impostor.glb") for kind in self.assets}
        from shapely.geometry import LineString
        trees = [{"id": "node/1", "geometry": Point(0, 0), "tags": {"height": "unknown"}}]
        rows = [{"id": "way/2", "geometry": LineString([(0, 0), (18, 0)]), "tags": {}}]
        terrain, _, _ = load_tree_map(self.map.path, self.map.config)
        records = tree_instances(trees, rows, terrain, assets)
        self.assertEqual(len(records), 3)
        self.assertEqual([r["position"][0] for r in records], [0, 9, 18])
        self.assertEqual(records[1]["source"]["osm_id"], "way/2")
        self.assertEqual(records[0]["dimensions_m"][1], assets["tree"]["visible_height_m"])

    def test_map_changed_mid_preparation_keeps_previous_pair(self):
        self.prepare()
        originals = {p: p.read_bytes() for p in self.output.glob("*.json")}
        from src.osm_vegetation import parse_vegetation as parse
        def mutate(payload, frame):
            self.map.path.write_bytes(self.map.path.read_bytes() + b'changed')
            return parse(payload, frame)
        with patch("src.osm_vegetation_placements.parse_vegetation", side_effect=mutate):
            with self.assertRaisesRegex(ValueError, "changed during"):
                self.prepare()
        for path, original in originals.items():
            self.assertEqual(path.read_bytes(), original)
        self.assertEqual(list(self.output.glob("*.part")), [])

    def test_second_publication_failure_rolls_back_pair(self):
        self.prepare()
        originals = {p: p.read_bytes() for p in self.output.glob("*.json")}
        node = copy.deepcopy(self.node)
        node["tags"]["height"] = "25"
        self.input.write_text(json.dumps({"elements": [node]}))
        replace = Path.replace
        def fail_second(path, target):
            if target == self.output / "forest_patches.json":
                raise OSError("simulated publication failure")
            return replace(path, target)
        with patch.object(Path, "replace", fail_second):
            with self.assertRaisesRegex(OSError, "simulated"):
                self.prepare()
        for path, original in originals.items():
            self.assertEqual(path.read_bytes(), original)

    def test_cli_passes_config_and_map_to_preparation(self):
        with patch("tools.prepare_osm_vegetation.prepare_osm_vegetation", return_value={}) as prepare:
            self.assertEqual(main(["--config", str(self.root / "config.json"), "--map", str(self.map.path),
                                   "--input", str(self.input), "--offline"]), 0)
        self.assertEqual(prepare.call_args.kwargs["config_path"], self.root / "config.json")
        self.assertTrue(prepare.call_args.kwargs["offline"])


if __name__ == "__main__":
    unittest.main()
