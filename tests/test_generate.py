"""Isolated generator publication checks with small synthetic geodata."""

from contextlib import redirect_stdout
import hashlib
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
from src import fence_scene_update
from src.config import coordinate_frame
from src.glb import read_glb
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
            patch.object(generate, "download_osm", return_value=(self.osm_path, self.sources["provenance"]["osm"])),
            patch.object(generate, "build_terrain", return_value=(self.terrain, mesh, stats)),
            patch.object(generate, "prepare_trees", return_value={"instances": 17, "method": "tuned2"}),
        ):
            patcher.start()
            self.addCleanup(patcher.stop)

    def write_osm(self, include_railways=True, include_fences=False):
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
        if include_fences:
            elements.append(way(5, {"barrier": "fence", "height": "2", "material": "wood"},
                                [(-20, 0), (20, 0)]))
        self.osm_path.write_text(json.dumps({"elements": elements}, ensure_ascii=False), encoding="utf-8")

    def run_generator(self):
        stdout = io.StringIO()
        with redirect_stdout(stdout):
            report = generate.run(self.config)
        return report, json.loads(stdout.getvalue())

    def published_navigation(self):
        output = self.root / "output"
        return {name: (output / name).read_bytes() for name in (
            "godollo.glb", "report.json", "godollo.navmesh", "godollo.navmesh.json",
            "godollo.navigation.json")}

    def test_full_generation_publishes_matching_convex_navmesh(self):
        report, summary = self.run_generator()
        files = self.published_navigation()
        self.assertEqual(files["godollo.navmesh"][:8], b"GDNAV02\0")
        navigation = report["navigation"]
        self.assertEqual(navigation["map_sha256"], hashlib.sha256(files["godollo.glb"]).hexdigest())
        self.assertEqual(navigation["source_sha256"], hashlib.sha256(files["godollo.navigation.json"]).hexdigest())
        # Both versions keep the input replay hash in the last header field.
        self.assertEqual(files["godollo.navmesh"][96:128].hex(), navigation["source_sha256"])
        self.assertEqual(summary["navmesh"], str(self.root / "output/godollo.navmesh"))
        self.assertEqual(json.loads(files["report.json"])["navigation"], navigation)

    def test_failed_navmesh_bake_preserves_previous_outputs(self):
        self.run_generator()
        original = self.published_navigation()
        with patch.object(generate, "prepare_navmesh", side_effect=ValueError("invalid navigation")):
            with self.assertRaisesRegex(ValueError, "invalid navigation"):
                self.run_generator()
        self.assertEqual(self.published_navigation(), original)
        self.assertFalse(list((self.root / "output").glob(".map-*")))

    def test_full_publication_failure_restores_map_and_navigation(self):
        self.run_generator()
        original = self.published_navigation()
        self.config["roads"]["offset_m"] = .5
        target = self.root / "output/godollo.navmesh"
        replace = Path.replace

        def fail_navmesh(path, destination):
            if destination == target:
                raise OSError("navmesh publication failed")
            return replace(path, destination)

        with patch.object(Path, "replace", fail_navmesh):
            with self.assertRaisesRegex(OSError, "navmesh publication failed"):
                self.run_generator()
        self.assertEqual(self.published_navigation(), original)

    def test_full_generation_prepares_default_trees_for_verified_map(self):
        self.config.update(offline=True, _config_path=str(self.root/'area.json'))
        self.sources['sentinel'] = self.root/'current-sentinel.tif'
        def prepare(**options):
            self.assertTrue(options['map_path'].is_file())
            self.assertEqual(options['config_path'], self.root/'area.json')
            self.assertTrue(options['offline'])
            self.assertEqual(options['root'], self.root)
            self.assertEqual(options['image_path'], self.sources['sentinel'])
            return {'instances': 23}
        with patch.object(generate, 'prepare_trees', side_effect=prepare) as preparation:
            report, summary = self.run_generator()
        self.assertEqual(preparation.call_count, 1)
        self.assertEqual(report['trees']['instances'], 23)
        self.assertEqual(summary['tree_instances'], 23)

    def test_trees_only_reuses_map_without_building_or_downloading_town(self):
        self.run_generator()
        navigation = self.published_navigation()
        path = self.root/'output/godollo.glb'
        original = path.read_bytes()
        self.config['trees_only'] = True
        with patch.object(generate, 'download_sources', side_effect=AssertionError('No town downloads')), \
             patch.object(generate, 'build_terrain', side_effect=AssertionError('Reuse terrain')), \
             patch.object(generate, 'add_buildings', side_effect=AssertionError('Reuse buildings')):
            report, _ = self.run_generator()
        self.assertEqual(report['instances'], 17)
        self.assertEqual(path.read_bytes(), original)
        self.assertEqual(self.published_navigation(), navigation)

    def test_tree_preparation_error_is_not_a_successful_regeneration(self):
        with patch.object(generate, 'prepare_trees', side_effect=ValueError('incompatible Sentinel calibration')):
            with self.assertRaisesRegex(ValueError, 'incompatible Sentinel calibration'):
                self.run_generator()

    def test_explicit_geometry_only_generation_skips_tree_preparation(self):
        self.config['no_tree_preparation'] = True
        with patch.object(generate, 'prepare_trees', side_effect=AssertionError('Explicit geometry only')):
            report, summary = self.run_generator()
        self.assertNotIn('trees', report)
        self.assertNotIn('tree_instances', summary)
        self.assertTrue((self.root / "output/godollo.navmesh").is_file())

    def test_railways_reach_verified_glb_report_and_console(self):
        report, summary = self.run_generator()
        output = self.root / "output" / "godollo.glb"
        self.assertTrue(report["verification"]["valid"])
        self.assertEqual(report["verification"]["building_solids_checked"], 1)
        self.assertEqual(report["verification"]["detailed_building_solids_checked"], 1)
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
        self.assertEqual(report["buildings"]["doors"], 2)
        self.assertGreater(report["buildings"]["windows"], 0)
        self.assertIn("building_window_blue", report["verification"]["materials"])
        self.assertEqual(report["roads"]["road_segments"], 1)
        self.assertEqual(report["railways"]["railway_segments"], 0)
        self.assertEqual(summary["railway_segments"], 0)
        self.assertNotIn("railway_ballast", report["verification"]["materials"])
        scene = trimesh.load_scene(self.root / "output" / "godollo.glb", process=False)
        self.assertFalse(any(name.startswith("railways_") for name in scene.geometry))

    def test_fences_reach_verified_glb_report_and_gltf_coordinate_frame(self):
        self.write_osm(include_fences=True)
        report, summary = self.run_generator()
        self.assertTrue(report["verification"]["valid"])
        self.assertEqual(report["fences"]["fence_ways"], 1)
        self.assertEqual(summary["fence_ways"], 1)
        self.assertIn("fence_wood", report["verification"]["materials"])
        scene = trimesh.load_scene(self.root / "output" / "godollo.glb", process=False)
        self.assertEqual(scene.metadata["fences"], report["fences"])
        panel = scene.geometry["fences_wood"]
        node = next(n for n in scene.graph.nodes_geometry if scene.graph[n][1] == "fences_wood")
        world = trimesh.transform_points(panel.vertices, scene.graph[node][0])
        np.testing.assert_allclose(world[:, 2], 0, atol=1e-7)
        np.testing.assert_allclose(world[::2, 1], self.terrain(world[::2, 0], -world[::2, 2]) - .05, atol=2e-6)
        np.testing.assert_allclose(world[1::2, 1] - world[::2, 1], 2.05, atol=2e-6)

    def test_area_without_fences_still_publishes(self):
        report, summary = self.run_generator()
        self.assertEqual(report["fences"]["fence_ways"], 0)
        self.assertEqual(report["fences"]["fence_meshes"], 0)
        self.assertEqual(summary["fence_ways"], 0)

    def test_fences_only_preserves_town_geometry_and_replaces_existing_fences(self):
        original_report, _ = self.run_generator()
        original_navigation = (self.root / "output/godollo.navmesh").read_bytes()
        output = self.root / "output/godollo.glb"
        original = trimesh.load_scene(output, process=False)
        _, original_binary = read_glb(output.read_bytes())
        self.write_osm(include_fences=True)
        self.config["fences_only"] = True
        with patch.object(generate, "download_sources", side_effect=AssertionError("No raster downloads")), \
             patch.object(generate, "build_terrain", side_effect=AssertionError("Reuse published terrain")), \
             patch.object(generate, "add_buildings", side_effect=AssertionError("Reuse town geometry")):
            report, _ = self.run_generator()
            # Repeating the update must replace, rather than duplicate, fences.
            repeated, _ = self.run_generator()
        self.assertEqual(report["fences"], repeated["fences"])
        self.assertNotEqual((self.root / "output/godollo.navmesh").read_bytes(), original_navigation)
        self.assertEqual(repeated["navigation"]["map_sha256"], hashlib.sha256(output.read_bytes()).hexdigest())
        self.assertEqual(report["buildings"], original_report["buildings"])
        self.assertEqual(report["sources"]["osm"], original_report["sources"]["osm"])
        self.assertEqual(report["sources"]["fences"], self.sources["provenance"]["osm"])
        updated = trimesh.load_scene(output, process=False)
        _, updated_binary = read_glb(output.read_bytes())
        self.assertEqual(updated_binary[:len(original_binary)], original_binary)
        self.assertEqual(len(updated.geometry), len(original.geometry) + 1)
        for name, mesh in original.geometry.items():
            np.testing.assert_array_equal(updated.geometry[name].vertices, mesh.vertices)
            np.testing.assert_array_equal(updated.geometry[name].faces, mesh.faces)
            self.assertEqual(updated.geometry[name].metadata, mesh.metadata)
            self.assertEqual(updated.geometry[name].visual.material.name, mesh.visual.material.name)
            np.testing.assert_allclose(updated.graph[name][0], original.graph[name][0], atol=0)
        panel = updated.geometry["fences_wood"]
        world = trimesh.transform_points(panel.vertices, updated.graph["fences_wood"][0])
        np.testing.assert_allclose(world[:, 2], 0, atol=1e-7)
        np.testing.assert_allclose(world[::2, 1], self.terrain(world[::2, 0], 0) - .05, atol=2e-6)
        self.assertTrue((self.root / "output/fence-backups").is_dir())
        self.write_osm(include_fences=False)
        empty, _ = self.run_generator()
        self.assertEqual(empty["fences"]["fence_ways"], 0)
        cleared = trimesh.load_scene(output, process=False)
        self.assertEqual(set(cleared.geometry), set(original.geometry))

    def test_fence_patch_removes_legacy_solid_post_draws(self):
        original = trimesh.Scene(metadata={"sources": {}})
        original.add_geometry(trimesh.creation.box(), geom_name="town", node_name="town")
        original.add_geometry(trimesh.creation.box(), geom_name="fences_chain_link_posts",
                              node_name="fences_chain_link_posts")
        replacement = trimesh.Scene()
        panel = trimesh.creation.box()
        panel.visual = trimesh.visual.TextureVisuals(
            material=trimesh.visual.material.PBRMaterial(name="fence_chain_link"))
        replacement.add_geometry(panel, geom_name="fences_chain_link",
                                 node_name="fences_chain_link")
        patched = fence_scene_update.prepare_fence_patch(
            original.export(file_type="glb"), replacement, {}, {})
        loaded = trimesh.load_scene(io.BytesIO(patched), file_type="glb", process=False)
        self.assertEqual(set(loaded.geometry), {"town", "fences_chain_link"})
        self.assertFalse(any("_posts" in node for node in loaded.graph.nodes_geometry))

    def test_failed_fences_only_validation_preserves_existing_map_and_report(self):
        self.run_generator()
        output = self.root / "output/godollo.glb"
        report_path = self.root / "output/report.json"
        original, original_report = output.read_bytes(), report_path.read_bytes()
        self.write_osm(include_fences=True)
        self.config["fences_only"] = True
        with patch.object(fence_scene_update, "inspect_glb", side_effect=ValueError("invalid fence candidate")):
            with self.assertRaisesRegex(ValueError, "invalid fence candidate"):
                self.run_generator()
        self.assertEqual(output.read_bytes(), original)
        self.assertEqual(report_path.read_bytes(), original_report)
        self.assertFalse(list(output.parent.glob(".fences-*")))

    def test_fences_only_requires_an_existing_map(self):
        self.config["fences_only"] = True
        with self.assertRaisesRegex(ValueError, "requires an existing"):
            self.run_generator()

    def test_failed_fence_navmesh_bake_preserves_map_and_navigation(self):
        self.run_generator()
        original = self.published_navigation()
        self.write_osm(include_fences=True)
        self.config["fences_only"] = True
        with patch.object(fence_scene_update, "prepare_navmesh", side_effect=ValueError("invalid navigation")):
            with self.assertRaisesRegex(ValueError, "invalid navigation"):
                self.run_generator()
        self.assertEqual(self.published_navigation(), original)
        self.assertFalse(list((self.root / "output").glob(".fences-*")))

    def test_fences_only_report_publication_failure_rolls_back_map(self):
        self.run_generator()
        original_outputs = self.published_navigation()
        output = self.root / "output/godollo.glb"
        report_path = self.root / "output/report.json"
        original, original_report = output.read_bytes(), report_path.read_bytes()
        self.write_osm(include_fences=True)
        self.config["fences_only"] = True
        replace = Path.replace

        def fail_report(path, destination):
            if destination == report_path:
                raise OSError("report publication failed")
            return replace(path, destination)

        with patch.object(Path, "replace", fail_report):
            with self.assertRaisesRegex(OSError, "report publication failed"):
                self.run_generator()
        self.assertEqual(output.read_bytes(), original)
        self.assertEqual(report_path.read_bytes(), original_report)
        self.assertEqual(self.published_navigation(), original_outputs)
        self.assertFalse(list(output.parent.glob(".fences-*")))

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

    def test_missing_glass_fails_detailed_solid_validation(self):
        self.run_generator()
        scene = trimesh.load_scene(self.root / "output" / "godollo.glb", process=False)
        scene.delete_geometry("building_way_1_0_windows")
        candidate = self.root / "missing-glass.glb"
        candidate.write_bytes(scene.export(file_type="glb"))
        with self.assertRaisesRegex(ValueError, "detailed walls and infills"):
            generate.inspect_glb(candidate)


if __name__ == "__main__":
    unittest.main()
