"""Offline weapon export rejects invalid geometry before replacing good files."""
from copy import deepcopy
import json
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

from src.glb import read_glb
from tools.build_weapon_assets import WEAPON_IDS, build_model, main
from tools.prepare_grenade_model import grenade_model
from collections import Counter


def weapon(name="pistol"):
    return dict(id=name, parts=[dict(name="body", color=[.2, .3, .4],
                                     positions=[[0, 0, 0], [1, 0, 0], [0, 1, 0]],
                                     triangles=[[0, 1, 2]])])


class WeaponAssetTests(unittest.TestCase):
    def test_octagonal_grenade_source_is_closed_and_reproducible(self):
        model = grenade_model()
        source = Path(__file__).resolve().parents[1] / "assets/weapons/he_grenade.json"
        self.assertEqual(json.loads(source.read_text()), model)
        edges = Counter()
        for part in model["parts"]:
            points = part["positions"]
            self.assertEqual(len({(p[0], p[2]) for p in points}), 8)
            for triangle in part["triangles"]:
                for a, b in zip(triangle, triangle[1:] + triangle[:1]):
                    edges[tuple(sorted((tuple(points[a]), tuple(points[b]))))] += 1
        self.assertTrue(all(count == 2 for count in edges.values()), "Grenade shell must have no open seams")
        document, _ = read_glb(build_model(model))
        self.assertEqual(len(document["materials"]), 5)
    def test_self_contained_glb_with_source_bounds_and_material(self):
        document, binary = read_glb(build_model(weapon()))
        self.assertEqual(document["buffers"], [{"byteLength": len(binary)}])
        self.assertEqual(document["accessors"][0]["min"], [0, 0, 0])
        self.assertEqual(document["accessors"][0]["max"], [1, 1, 0])
        self.assertEqual(document["nodes"][0]["name"], "body")
        self.assertEqual(document["materials"][0]["pbrMetallicRoughness"]["baseColorFactor"], [.2, .3, .4, 1])

    def test_bad_geometry_fails_explicitly(self):
        original = weapon()
        for triangle in ([0, 0, 1], [-1, 1, 2], [0, 1, 3], [0, 1]):
            with self.subTest(triangle=triangle):
                bad = deepcopy(original)
                bad["parts"][0]["triangles"] = [triangle]
                with self.assertRaises(ValueError):
                    build_model(bad)
        bad = weapon()
        bad["parts"][0]["positions"][0][0] = float("nan")
        with self.assertRaisesRegex(ValueError, "positions"):
            build_model(bad)

    def test_invalid_last_weapon_preserves_all_previous_outputs(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            source = root / "models.json"
            document = dict(version=1, units="meters", axes="X right, Y up, muzzle -Z",
                            weapons=[weapon(name) for name in WEAPON_IDS])
            source.write_text(json.dumps(document))
            arguments = ["build_weapon_assets", "--source", str(source), "--output-dir", str(root / "models")]
            with patch.object(sys, "argv", arguments):
                main()
                previous = {p.name: p.read_bytes() for p in (root / "models").iterdir()}
                document["weapons"][0]["parts"][0]["color"] = [.9, .8, .7]
                document["weapons"][-1]["parts"][0]["triangles"] = [[0, 0, 1]]
                source.write_text(json.dumps(document))
                with self.assertRaisesRegex(ValueError, "Degenerate triangle"):
                    main()
            self.assertEqual({p.name: p.read_bytes() for p in (root / "models").iterdir()}, previous)


if __name__ == "__main__":
    unittest.main()
