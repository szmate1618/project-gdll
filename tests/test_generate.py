"""Isolated generator publication checks with small synthetic geodata."""

from contextlib import redirect_stdout
import io
import json
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

import numpy as np
from PIL import Image
from pyproj import Transformer
import trimesh

from src import generate
from src.config import coordinate_frame
from src.terrain import Terrain


class GenerateTests(unittest.TestCase):
    def setUp(self):
        # generate.run writes several project paths; every output stays isolated
        # from the configured map, source caches, and attribution document.
        directory = tempfile.TemporaryDirectory(prefix="generator-test-", dir="/tmp")
        self.addCleanup(directory.cleanup)
        self.root = Path(directory.name)
        (self.root / "ATTRIBUTION.md").write_text("Synthetic offline fixture.\n", encoding="utf-8")
        self.config = {
            "bbox": [19.35, 47.58, 19.351, 47.581],
            "crs": "EPSG:32634",
            "terrain": {"spacing_m": 15},
            "roads": {"sample_spacing_m": 8, "offset_m": .25},
            "railways": {"width_m": 4, "sample_spacing_m": 8, "offset_m": .3},
        }
        self.frame = coordinate_frame(self.config)
        xmin, ymin, xmax, ymax = self.frame["bounds"]
        xx, yy = np.meshgrid(np.linspace(xmin, xmax, 3), np.linspace(ymax, ymin, 3))
        # A nonzero absolute datum detects applying the datum a second time.
        self.terrain = Terrain(.03 * xx + .05 * yy, self.frame["bounds"], datum=234)
        mesh = self.terrain.mesh(Image.new("RGB", (4, 4), (35, 85, 45)))
        stats = {"dimensions_m": [xmax - xmin, ymax - ymin], "grid_vertices": [3, 3]}
        self.osm_path = self.root / "osm.json"
        self.write_osm()
        self.sources = {"osm": self.osm_path, "provenance": {"osm": {"fixture": True}}}
        for patcher in (
            patch.object(generate, "ROOT", self.root),
            patch.object(generate, "download_sources", return_value=self.sources),
            patch.object(generate, "build_terrain", return_value=(self.terrain, mesh, stats)),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def write_osm(self, include_railways=True):
        inverse = Transformer.from_crs(self.config["crs"], "EPSG:4326", always_xy=True)
        ox, oy = self.frame["origin"]

        def way(identifier, tags, coordinates):
            geometry = []
            for x, y in coordinates:
                lon, lat = inverse.transform(ox + x, oy + y)
                geometry.append({"lon": lon, "lat": lat})
            return {"type": "way", "id": identifier, "tags": tags, "geometry": geometry}

        elements = [
            way(1, {"building": "yes", "roof:shape": "flat"},
                [(0, -15), (10, -15), (10, -5), (0, -5), (0, -15)]),
            way(2, {"highway": "residential"}, [(-20, -25), (20, -25)]),
        ]
        if include_railways:
            elements.extend([
                way(3, {"railway": "rail", "operator": "MÁV"}, [(-20, 20), (20, 20)]),
                way(4, {"railway": "light_rail", "operator": "MÁV-HÉV"}, [(-20, 32), (20, 32)]),
            ])
        self.osm_path.write_text(json.dumps({"elements": elements}, ensure_ascii=False), encoding="utf-8")

    def run_generator(self):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            report = generate.run(self.config)
        return report, json.loads(stdout.getvalue())

    def test_railways_reach_verified_glb_report_and_console(self):
        report, summary = self.run_generator()
        output = self.root / "output" / "godollo.glb"
        self.assertTrue(report["verification"]["valid"])
        self.assertEqual(report["verification"]["building_solids_checked"], 1)
        self.assertEqual(report["railways"]["railway_segments"], 2)
        self.assertEqual(summary["railway_segments"], 2)
        self.assertIn("railway_ballast", report["verification"]["materials"])
        saved = json.loads((self.root / "output" / "report.json").read_text(encoding="utf-8"))
        self.assertEqual(saved, report)
        data = output.read_bytes()
        json_length = struct.unpack_from("<I", data, 12)[0]
        document = json.loads(data[20:20 + json_length])
        metadata = document["scenes"][document.get("scene", 0)]["extras"]
        self.assertEqual(metadata["railways"], report["railways"])

        scene = trimesh.load_scene(output, process=False)
        railway = scene.geometry["railways_ballast"]
        self.assertEqual(railway.visual.material.name, "railway_ballast")
        railway_nodes = [node for node in scene.graph.nodes_geometry
                         if scene.graph[node][1] == "railways_ballast"]
        self.assertEqual(len(railway_nodes), 1)
        world = trimesh.transform_points(railway.vertices, scene.graph[railway_nodes[0]][0])
        # Both paths retain their northing after conversion to X-east/Y-up/Z-south.
        north = -world[:, 2]
        self.assertTrue(np.any(np.isclose(north, 18)))
        self.assertTrue(np.any(np.isclose(north, 34)))
        np.testing.assert_allclose(world[:, 1], self.terrain(world[:, 0], north) + .3, atol=2e-6)
        self.assertFalse(output.with_suffix(".pending.glb").exists())

    def test_bbox_without_railways_still_publishes_buildings_and_roads(self):
        self.write_osm(include_railways=False)
        report, summary = self.run_generator()
        self.assertTrue(report["verification"]["valid"])
        self.assertEqual(report["buildings"]["buildings"], 1)
        self.assertEqual(report["roads"]["road_segments"], 1)
        self.assertEqual(report["railways"]["railway_segments"], 0)
        self.assertEqual(summary["railway_segments"], 0)
        self.assertNotIn("railway_ballast", report["verification"]["materials"])
        scene = trimesh.load_scene(self.root / "output" / "godollo.glb", process=False)
        self.assertFalse(any(name.startswith("railways_") for name in scene.geometry))

    def test_failed_validation_preserves_published_map_and_report(self):
        output = self.root / "output"
        output.mkdir()
        published = output / "godollo.glb"
        published.write_bytes(b"previous map contents")
        report_path = output / "report.json"
        report_path.write_text('{"previous": true}\n', encoding="utf-8")
        with patch.object(generate, "inspect_glb", side_effect=ValueError("invalid candidate")):
            with self.assertRaisesRegex(ValueError, "invalid candidate"):
                self.run_generator()
        self.assertEqual(published.read_bytes(), b"previous map contents")
        self.assertEqual(report_path.read_text(encoding="utf-8"), '{"previous": true}\n')
        self.assertFalse(published.with_suffix(".pending.glb").exists())


if __name__ == "__main__":
    unittest.main()
