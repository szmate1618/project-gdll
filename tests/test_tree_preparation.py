"""Default tree publication preserves old data and runtime file references."""
import hashlib
import json
from pathlib import Path
import unittest
from unittest.mock import patch

from src.tree_preparation import prepare_trees, publish_default_trees
from src.tree_assets import load_impostor
from tests.satellite_tree_fixtures import SatelliteTreeFixture
from tools.prepare_trees import main


class DefaultTreePreparationTests(SatelliteTreeFixture, unittest.TestCase):
    def setUp(self):
        super().setUp()
        self.target = self.map_path.parent / "trees.instances.json"
        self.original = b"original detector placements preserved verbatim\n"
        self.target.write_bytes(self.original)
        exclusions = self.root / "osm/forest_patches.json"
        exclusions.parent.mkdir()
        exclusions.write_text(json.dumps({"source": {"map_sha256": hashlib.sha256(self.map_path.read_bytes()).hexdigest()}}))
        self.write_layer(exclusions.parent)

    def write_layer(self, directory):
        directory.mkdir(parents=True, exist_ok=True)
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        import os
        document = {"schema": "godollo.tree-instances", "version": 1,
                    "map": {"path": os.path.relpath(self.map_path, directory),
                            "map_sha256": digest(self.map_path), "crs": self.frame["crs"],
                            "origin_projected_m": list(self.frame["origin"]), "units": "meters"},
                    "assets": {"tree": {"path": os.path.relpath(self.asset, directory),
                                        "sha256": digest(self.asset)}},
                    "source": {"method": "astra_recall_with_negative_colour_context_filter"},
                    "instances": [], "transforms": {"matrix_authoritative": True, "units": "meters",
                        "matrix_layout": "column-major", "axes": {"X": "east", "Y": "up", "Z": "south"}}}
        path = directory / "trees.instances.json"
        path.write_text(json.dumps(document))
        return path

    def test_default_command_uses_osm_and_publishes_portable_runtime_paths(self):
        def detect(exclusions, **options):
            self.assertEqual(exclusions, self.map_path.parent / "osm/forest_patches.json")
            self.assertEqual(options["config_path"], self.config)
            self.write_layer(options["output_dir"])
            return {"output": str(options["output_dir"]), "instances": 0}
        with patch("src.tree_preparation.prepare_tuned2_trees", side_effect=detect):
            self.assertEqual(main(["--map", str(self.map_path), "--config", str(self.config)]), 0)
        document = json.loads(self.target.read_text())
        self.assertEqual(document["map"]["path"], "map.glb")
        self.assertEqual(document["assets"]["tree"]["path"], "tree.glb")
        self.assertEqual(document["source"]["default_preparation"], "osm-tuned2")
        self.assertEqual(self.target.with_name("trees.original.instances.json").read_bytes(), self.original)
        self.assertTrue((self.root / "satellite_trees/tuned2-osm/trees.instances.json").exists())

    def test_explicit_exclusions_still_supported(self):
        def detect(exclusions, **options):
            self.assertEqual(exclusions, self.forest_path)
            self.write_layer(options["output_dir"])
            return {"output": str(options["output_dir"]), "instances": 0}
        with patch("src.tree_preparation.prepare_tuned2_trees", side_effect=detect):
            result = prepare_trees(map_path=self.map_path, forest_exclusions=self.forest_path)
        self.assertEqual(result["forest_exclusions"], str(self.forest_path))

    def test_stale_default_osm_exclusions_are_prepared_for_current_map(self):
        exclusions = self.root / "osm/forest_patches.json"
        exclusions.write_text(json.dumps({"source": {"map_sha256": "old map"}}))
        def detect(path, **options):
            self.write_layer(options["output_dir"])
            return {"output": str(options["output_dir"]), "instances": 0}
        with patch("src.tree_preparation.prepare_osm_vegetation") as osm, \
             patch("src.tree_preparation.prepare_tuned2_trees", side_effect=detect):
            prepare_trees(map_path=self.map_path, config_path=self.config, root=self.root, offline=True)
        self.assertEqual(osm.call_args.kwargs['map_path'], self.map_path)
        self.assertEqual(osm.call_args.kwargs['output_dir'], exclusions.parent)
        self.assertTrue(osm.call_args.kwargs['offline'])

    def test_stale_map_or_asset_does_not_replace_default(self):
        path = self.write_layer(self.output)
        document = json.loads(path.read_text())
        for section, field in (("map", "map_sha256"), ("asset", "sha256")):
            with self.subTest(section=section):
                changed = json.loads(json.dumps(document))
                record = changed["map"] if section == "map" else changed["assets"]["tree"]
                record[field] = "stale"
                path.write_text(json.dumps(changed))
                with self.assertRaisesRegex(ValueError, "match|stale"):
                    publish_default_trees(path, self.map_path)
                self.assertEqual(self.target.read_bytes(), self.original)

    def test_failed_publication_and_repeated_runs_preserve_backups(self):
        path = self.write_layer(self.output)
        with patch("src.tree_preparation.os.replace", side_effect=OSError("publication failed")):
            with self.assertRaisesRegex(OSError, "publication failed"):
                publish_default_trees(path, self.map_path)
        self.assertEqual(self.target.read_bytes(), self.original)
        publish_default_trees(path, self.map_path)
        first = self.target.read_bytes()
        self.assertIsNone(publish_default_trees(path, self.map_path)["backup"])
        document = json.loads(path.read_text())
        document["source"]["revision"] = 2
        path.write_text(json.dumps(document))
        result = publish_default_trees(path, self.map_path)
        self.assertEqual(Path(result["backup"]).read_bytes(), first)
        self.assertEqual(self.target.with_name("trees.original.instances.json").read_bytes(), self.original)

    def test_input_changed_during_staging_keeps_default(self):
        path = self.write_layer(self.output)
        import os
        link = os.link
        def change_input_after_backup(source, target):
            link(source, target)
            path.write_bytes(path.read_bytes() + b" ")
        with patch("src.tree_preparation.os.link", side_effect=change_input_after_backup):
            with self.assertRaisesRegex(ValueError, "input changed"):
                publish_default_trees(path, self.map_path)
        self.assertEqual(self.target.read_bytes(), self.original)

    def test_pine_assignment_on_publication_and_repeat_in_place(self):
        import os
        from tools.create_tree_impostors import build_impostor
        pine_path = self.root / 'pine.glb'
        pine_path.write_bytes(build_impostor('pine', 14.))
        path = self.write_layer(self.output)
        document = json.loads(path.read_text())
        for name, asset_path in (('tree', self.asset), ('pine', pine_path)):
            document['assets'][name] = {**load_impostor(asset_path),
                                        'path': os.path.relpath(asset_path, path.parent)}
        tree = document['assets']['tree']
        document['instances'] = []
        for i in range(20):
            position = [i * 100., 0, 0]
            document['instances'].append({'id': f'mapped-tree-{i}', 'asset': 'tree',
                'matrix': [1,0,0,0, 0,1,0,0, 0,0,1,0, *position,1],
                'position': position, 'dimensions_m': [tree['footprint_m'][0], 10., tree['footprint_m'][1]],
                'bounds': {key: [v + p for v, p in zip(bounds, position)]
                           for key, bounds in tree['geometry_bounds_m'].items()}})
        path.write_text(json.dumps(document))
        result = publish_default_trees(path, self.map_path)
        self.assertEqual(result['new_pines'], 2)
        published = json.loads(self.target.read_text())
        self.assertEqual(sum(t['asset'] == 'pine' for t in published['instances']), 2)
        self.assertEqual(len(published['source']['pine_clusters']['center_ids']), 2)
        self.assertEqual(published['instances'][0]['dimensions_m'], document['instances'][0]['dimensions_m'])
        self.assertEqual((self.target.parent / published['assets']['pine']['path']).resolve(), pine_path)
        self.assertEqual(self.target.with_name('trees.original.instances.json').read_bytes(), self.original)
        before = self.target.read_bytes()
        repeated = publish_default_trees(self.target, self.map_path)
        self.assertEqual(repeated['new_pines'], 0)
        self.assertIsNone(repeated['backup'])
        self.assertEqual(self.target.read_bytes(), before)
        pine_path.write_bytes(b'corrupted pine GLB')
        with self.assertRaisesRegex(ValueError, 'stale'):
            publish_default_trees(self.target, self.map_path)
        self.assertEqual(self.target.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
