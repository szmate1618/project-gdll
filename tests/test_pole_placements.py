"""Offline pole query, georeference, deduplication and publication regressions."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock, patch

import numpy as np
from PIL import Image
import requests
import trimesh

from src.config import coordinate_frame, load_config
from src.data import SourceError, _download_osm_query
from src.pole_placements import make_columns, pole_query, prepare_poles
from src.terrain import Terrain


class PolePlacementTests(unittest.TestCase):
    def setUp(self):
        scratch = Path(__file__).resolve().parents[1] / "build"
        scratch.mkdir(exist_ok=True)
        self.temporary = tempfile.TemporaryDirectory(dir=scratch)
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.config_path = self.root / "config.json"
        self.config_path.write_text(json.dumps({"bbox": [19.35, 47.59, 19.36, 47.60]}))
        self.config = load_config(self.config_path)
        self.frame = coordinate_frame(self.config)

    @staticmethod
    def node(id=1, **tags):
        return {"type": "node", "id": id, "lon": 19.356, "lat": 47.597, "tags": tags}

    def test_all_tags_and_overpass_bbox_order(self):
        query = pole_query([19, 47, 20, 48])
        for selector in ('node["highway"="street_lamp"]', 'node["power"~"^(pole|tower)$"]',
                         'node["man_made"="utility_pole"]'):
            self.assertIn(selector + "(47,19,48,20);", query)
        self.assertNotIn("way[", query)

    def test_shared_pole_and_lamp_is_one_column_and_units_are_meters(self):
        nodes = [self.node(power="pole", highway="street_lamp", height="30 ft"),
                 self.node(power="pole", highway="street_lamp", height="30 ft"),
                 self.node(2, power="tower"), self.node(3, highway="street_lamp", height="nonsense"),
                 self.node(4, man_made="utility_pole")]
        terrain = Mock(return_value=12.5)
        columns = make_columns({"elements": nodes}, self.config, terrain, self.frame)
        self.assertEqual(len(columns), 4)
        self.assertEqual([c["kind"] for c in columns],
                         ["power_pole", "power_tower", "street_lamp", "utility_pole"])
        self.assertAlmostEqual(columns[0]["height_m"], 9.144)
        self.assertEqual(columns[1]["height_m"], 25)
        self.assertEqual(columns[2]["height_m"], 7)
        # This node is northeast of the configured center: runtime east +X,
        # north -Z. The datum must not be subtracted from terrain height again.
        self.assertGreater(columns[0]["position"][0], 0)
        self.assertLess(columns[0]["position"][2], 0)
        self.assertEqual(columns[0]["position"][1], 12.5)
        self.assertEqual(terrain.call_args.args[1], -columns[0]["position"][2])

    def test_outside_nodes_are_filtered_and_bad_coordinates_fail(self):
        node = self.node(highway="street_lamp")
        node["lon"] = 20
        self.assertEqual(make_columns({"elements": [node]}, self.config, Mock(), self.frame), [])
        for invalid in (float("nan"), "19.356", True):
            node["lon"] = invalid
            with self.assertRaisesRegex(ValueError, "coordinates"):
                make_columns({"elements": [node]}, self.config, Mock(), self.frame)

    def test_pole_query_cache_is_separate_and_reusable_offline(self):
        response = Mock()
        response.json.return_value = {"elements": []}
        session = Mock(spec=requests.Session)
        session.request.return_value = response
        query = pole_query(self.config["bbox"])
        path, _ = _download_osm_query(self.config, self.root, session, query, "poles", prefix="poles")
        self.assertTrue(path.name.startswith("poles_"))
        session.request.reset_mock()
        config = {**self.config, "offline": True}
        _download_osm_query(config, self.root, session, query, "poles", prefix="poles")
        session.request.assert_not_called()
        with self.assertRaises(SourceError):
            _download_osm_query(config, self.root, session, query + "\n", "poles", prefix="poles")

    def write_map(self, osm_source=None):
        terrain = Terrain(np.array([[10., 20.], [30., 50.]]), self.frame["bounds"], datum=200)
        scene = trimesh.Scene(base_frame="local_meters")
        scene.add_geometry(terrain.mesh(Image.new("RGB", (2, 2))), geom_name="terrain", node_name="terrain")
        scene.graph.update(frame_from="world", frame_to="local_meters",
                           matrix=np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]]))
        scene.graph.base_frame = "world"
        scene.metadata.update(bbox_wgs84=self.config["bbox"], crs=self.config["crs"],
                              origin_wgs84=list(self.frame["center"]), origin_projected_m=list(self.frame["origin"]),
                              origin_elevation_m=200, units="meters",
                              gltf_axes={"X": "projected east", "Y": "up", "Z": "projected south"},
                              terrain={"grid_vertices": [2, 2]})
        if osm_source:
            scene.metadata["sources"] = {"osm": osm_source}
        path = self.root / "map.glb"
        path.write_bytes(scene.export(file_type="glb"))
        return path

    def test_preparation_uses_published_map_and_preserves_last_layer_on_failure(self):
        map_path = self.write_map()
        original_map = map_path.read_bytes()
        payload = self.root / "nodes.json"
        payload.write_text(json.dumps({"elements": [self.node(highway="street_lamp")]}))
        output = self.root / "poles.instances.json"
        with patch("src.pole_placements._download_osm_query", return_value=(payload, {})):
            result = prepare_poles(config_path=self.config_path, map_path=map_path, root=self.root)
        self.assertEqual(result["columns"], 1)
        document = json.loads(output.read_text())
        self.assertEqual(document["map"]["map_sha256"], hashlib.sha256(original_map).hexdigest())
        self.assertGreater(document["columns"][0]["position"][1], 0)
        self.assertEqual(map_path.read_bytes(), original_map)
        last_layer = output.read_bytes()
        payload.write_text(json.dumps({"elements": [{**self.node(highway="street_lamp"), "lon": "bad"}]}))
        with patch("src.pole_placements._download_osm_query", return_value=(payload, {})):
            with self.assertRaises(ValueError):
                prepare_poles(config_path=self.config_path, map_path=map_path, root=self.root)
        self.assertEqual(output.read_bytes(), last_layer)

    def test_preparation_reuses_recorded_railways_and_replaces_derived_columns(self):
        directory = self.root / "data/raw/osm"
        directory.mkdir(parents=True)
        inverse = self.frame["transformer"]
        from pyproj.enums import TransformDirection
        tracks = []
        for id, north in enumerate((-2, 2), 1):
            coords = [inverse.transform(x + self.frame["origin"][0], north + self.frame["origin"][1],
                                        direction=TransformDirection.INVERSE) for x in (-100, 100)]
            tracks.append({"type": "way", "id": id,
                           "tags": {"railway": "light_rail", "operator": "MÁV-HÉV", "usage": "main"},
                           "geometry": [{"lon": lon, "lat": lat} for lon, lat in coords]})
        source = directory / "town.json"
        source.write_text(json.dumps({"elements": tracks, "osm3s": {"timestamp_osm_base": "snapshot"}}))
        # Relocated checkouts must find the recorded basename in their own cache.
        map_path = self.write_map({"path": "/old-checkout/data/raw/osm/town.json",
                                   "timestamp_osm_base": "snapshot"})
        payload = self.root / "nodes.json"
        payload.write_text(json.dumps({"elements": [self.node(highway="street_lamp")]}))
        with patch("src.pole_placements._download_osm_query", return_value=(payload, {})):
            for _ in range(2):
                result = prepare_poles(config_path=self.config_path, map_path=map_path, root=self.root)
                self.assertEqual(result["hev_catenary"], 4)
                self.assertEqual(result["columns"], 5)
        document = json.loads((self.root / "poles.instances.json").read_text())
        self.assertEqual(document["version"], 2)
        self.assertEqual(document["hev"]["spacing_m"], 50)
        last_layer = (self.root / "poles.instances.json").read_bytes()
        source.write_text(json.dumps({"elements": tracks, "osm3s": {"timestamp_osm_base": "newer"}}))
        with patch("src.pole_placements._download_osm_query", return_value=(payload, {})):
            with self.assertRaisesRegex(ValueError, "snapshot"):
                prepare_poles(config_path=self.config_path, map_path=map_path, root=self.root)
        self.assertEqual((self.root / "poles.instances.json").read_bytes(), last_layer)


if __name__ == "__main__":
    unittest.main()
