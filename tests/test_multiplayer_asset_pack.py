"""Portable asset exports preserve fingerprints and exclude preparation inputs."""
import hashlib
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from tools.package_multiplayer_assets import package_assets
from src.asset_credits import ASSET_CREDIT_FILES, SOFTWARE_CREDIT_FILES


def glb(document):
    text = json.dumps({"asset": {"version": "2.0"}, **document}).encode()
    text += b" " * (-len(text) % 4)
    return struct.pack("<4sIII4s", b"glTF", 2, 20 + len(text), len(text), b"JSON") + text


class MultiplayerAssetPackTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.map = self.write("output/town.glb", glb({"scenes": [{"extras": {
            "sources": {"osm": {}, "copernicus_dem": {}, "sentinel": {
                "datetime": "2021-08-13T09:47:43Z", "item_id": "selected-sentinel-item"}},
            "attribution": "Additional notice from the map's author."}}]}))
        shutil.copytree(Path(__file__).resolve().parents[1] / "credits", self.root / "credits")
        self.write("output/ATTRIBUTION.md", b"STALE adjacent notice for 1999")
        self.output = self.root / "build/assets.zip"
        self.zombies = self.root / "assets/zombies/models"
        self.tree = self.write("assets/tree.glb", glb({"extras": {"tree": True}}))
        self.write("assets/dry_tree_impostor.glb", glb({"extras": {"dry_tree": True}}))
        self.zombie = self.write("assets/zombies/models/Zombie.glb", glb({}))
        self.write("assets/zombies/models/unused-old-lod.glb.bak", b"not runtime")
        levels = []
        for level in range(1, 4):
            p = self.write(f"assets/zombies/models/lod{level}/Zombie.glb", glb({"extras": {"level": level}}))
            levels.append({"path": f"lod{level}/Zombie.glb", "sha256": self.digest(p)})
        self.json("assets/zombies/models/lods.json", {"version": 1, "models": {
            "Zombie.glb": {"source_sha256": self.digest(self.zombie), "levels": levels}}})
        self.json("output/trees.instances.json", {"map": {"map_sha256": self.digest(self.map)}, "assets": {
            "tree": {"path": "../assets/tree.glb", "sha256": self.digest(self.tree)}}, "instances": [], "source": {"default_preparation": "osm-tuned2"}})
        self.json("output/poles.instances.json", {"map": {"map_sha256": self.digest(self.map)}})
        self.write("output/guardrails.glb", glb({"scenes": [{"extras": {"map_sha256": self.digest(self.map)}}]}))
        self.json("forest_patches.json", {"source": {"map_sha256": self.digest(self.map)}, "runtime": {
            "tree_asset": "assets/tree.glb", "side_texture": "assets/side.png", "top_texture": "assets/top.png"}})
        self.json("output/osm/forest_patches.json", {"source": {"map_sha256": self.digest(self.map)}, "runtime": {
            "tree_asset": "../../assets/tree.glb", "side_texture": "../../assets/side.png", "top_texture": "../../assets/top.png"}})
        self.write("assets/side.png", b"side texture")
        self.write("assets/top.png", b"top texture")
        self.write("assets/shrub.png", b"shrub atlas")
        self.write("inventory.toml", b"inventory\n")
        for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"):
            self.write(f"items/{name}.toml", b"weapon\n")
        self.write("data/raw/input.bin", b"private preparation input")
        self.write("output/backups/town.glb", b"old town")
        self.write("assets/zombies/source/original.bin", b"source asset")

    def write(self, name, data):
        path = self.root / name
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_bytes(data)
        return path

    def json(self, name, data):
        return self.write(name, json.dumps(data).encode())

    @staticmethod
    def digest(path):
        return hashlib.sha256(path.read_bytes()).hexdigest()

    def package(self, **kwargs):
        return package_assets(self.root, self.map, self.output, self.zombies, **kwargs)

    def test_relocated_pack_keeps_referenced_bytes_and_only_runtime_files(self):
        count, size = self.package()
        destination = self.root / "extracted"
        with zipfile.ZipFile(self.output) as archive:
            archive.extractall(destination)
            manifest = json.loads(archive.read("asset-pack.json"))
            self.assertEqual(count, len(manifest["files"]))
            self.assertEqual(size, sum(archive.getinfo(name).file_size for name in manifest["files"]))
        self.assertEqual(manifest["map"], "output/town.glb")
        self.assertEqual(manifest["vegetation"], "osm-tuned2")
        expected = {"output/town.glb", "output/ATTRIBUTION.md", "output/trees.instances.json", "output/poles.instances.json", "output/guardrails.glb",
                    "assets/tree.glb", "assets/dry_tree_impostor.glb", "output/osm/forest_patches.json", "assets/side.png", "assets/top.png", "assets/shrub.png", "inventory.toml",
                    "assets/zombies/models/Zombie.glb", "assets/zombies/models/lods.json"}
        expected.update(f"items/{name}.toml" for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"))
        expected.update(f"assets/zombies/models/lod{level}/Zombie.glb" for level in range(1, 4))
        expected.update(f"credits/assets/{name}" for name in (*ASSET_CREDIT_FILES, "MAP.txt", "VEGETATION.txt"))
        expected.update(f"credits/software/{name}" for name in SOFTWARE_CREDIT_FILES)
        self.assertEqual(set(manifest["files"]), expected)
        for name, digest in manifest["files"].items():
            self.assertNotIn("\\", name)
            self.assertFalse(Path(name).is_absolute())
            if name not in ("credits/assets/MAP.txt", "credits/assets/VEGETATION.txt"):
                self.assertEqual((destination / name).read_bytes(), (self.root / name).read_bytes())
            self.assertEqual(self.digest(destination / name), digest)
        credit = (destination / "credits/assets/MAP.txt").read_text(encoding="utf-8")
        self.assertIn("Contains modified Copernicus Sentinel data 2021", credit)
        self.assertIn("Additional notice from the map's author.", credit)
        self.assertIn("© OpenStreetMap contributors", credit)
        self.assertIn("Copernicus DEM GLO-30", credit)
        self.assertIn(self.digest(self.map), credit)
        self.assertNotIn("1999", credit)
        tree = json.loads((destination / "output/trees.instances.json").read_text())
        tree_path = destination / "output" / tree["assets"]["tree"]["path"]
        self.assertEqual(self.digest(tree_path), tree["assets"]["tree"]["sha256"])

    def test_missing_or_stale_dependency_preserves_previous_archive(self):
        self.package()
        previous = self.output.read_bytes()
        for path in (self.map, self.tree, self.zombie, self.zombies / "lod1/Zombie.glb"):
            with self.subTest(path=path):
                data = path.read_bytes()
                path.write_bytes(glb({"extras": {"changed": True}}))
                with self.assertRaisesRegex(ValueError, "fingerprint"):
                    self.package()
                path.write_bytes(data)
                self.assertEqual(self.output.read_bytes(), previous)
        (self.root / "assets/side.png").unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime asset"):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)

    def test_failed_zip_verification_preserves_previous_archive(self):
        self.package()
        previous = self.output.read_bytes()
        with patch("tools.package_multiplayer_assets.zipfile.ZipFile.testzip", return_value="output/town.glb"):
            with self.assertRaisesRegex(ValueError, "Archive verification failed"):
                self.package()
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_missing_shrub_preserves_archive_and_explicit_override_is_packaged(self):
        self.package()
        previous = self.output.read_bytes()
        (self.root / "assets/shrub.png").unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime asset"):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)
        path = self.root / "output/osm/forest_patches.json"
        document = json.loads(path.read_text())
        document["runtime"]["shrub_texture"] = "../../assets/understory.png"
        self.json("output/osm/forest_patches.json", document)
        self.write("assets/understory.png", b"custom shrub atlas")
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read("assets/understory.png"), b"custom shrub atlas")
            self.assertNotIn("assets/shrub.png", archive.namelist())

    def test_legacy_shrub_configuration_and_atlas_names(self):
        path = self.root / "output/osm/forest_patches.json"
        cases = [
            ({}, "bush.png"),
            ({"bush_texture": "../../assets/shrub.png"}, "shrub.png"),
            ({"bush_texture": "../../assets/bush.png"}, "shrub.png"),
            ({"shrub_texture": "../../assets/shrub.png"}, "bush.png"),
            ({"shrub_texture": "../../assets/shrub.png",
              "bush_texture": "../../assets/missing.png"}, "shrub.png"),
        ]
        for settings, atlas in cases:
            with self.subTest(settings=settings, atlas=atlas):
                for name in ("shrub.png", "bush.png"):
                    (self.root / "assets" / name).unlink(missing_ok=True)
                self.write(f"assets/{atlas}", b"shrub atlas")
                document = json.loads(path.read_text())
                document["runtime"].pop("shrub_texture", None)
                document["runtime"].pop("bush_texture", None)
                document["runtime"].update(settings)
                self.json("output/osm/forest_patches.json", document)
                original = path.read_bytes()
                self.package()
                with zipfile.ZipFile(self.output) as archive:
                    self.assertEqual(archive.read(f"assets/{atlas}"), b"shrub atlas")
                    self.assertEqual(archive.read("output/osm/forest_patches.json"), original)
                self.assertEqual(path.read_bytes(), original)

    def test_missing_dry_tree_preserves_archive_and_override_is_packaged(self):
        self.package()
        previous = self.output.read_bytes()
        (self.root / "assets/dry_tree_impostor.glb").unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime asset"):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)
        path = self.root / "output/osm/forest_patches.json"
        document = json.loads(path.read_text())
        document["runtime"]["dry_tree_asset"] = "../../assets/custom_dry.glb"
        self.json("output/osm/forest_patches.json", document)
        self.write("assets/custom_dry.glb", glb({"extras": {"custom_dry_tree": True}}))
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn("assets/custom_dry.glb", archive.namelist())
            self.assertNotIn("assets/dry_tree_impostor.glb", archive.namelist())

    def test_missing_credit_preserves_previous_archive(self):
        self.package()
        previous = self.output.read_bytes()
        (self.root / "credits/assets/SENTINEL.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime asset"):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)

    def test_missing_bundled_component_notice_preserves_previous_pack(self):
        self.package()
        previous = self.output.read_bytes()
        (self.root/'credits/software/stb.txt').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing runtime asset'):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)

    def test_input_changed_during_zip_write_is_not_published(self):
        self.package()
        previous = self.output.read_bytes()
        original_open = zipfile.ZipFile.open

        def change_map(archive, name, mode="r", **kwargs):
            if name == "output/town.glb" and mode == "w":
                self.map.write_bytes(glb({"extras": {"changed_during_export": True}}))
            return original_open(archive, name, mode, **kwargs)

        with patch("tools.package_multiplayer_assets.zipfile.ZipFile.open", new=change_map):
            with self.assertRaisesRegex(ValueError, "changed during packaging"):
                self.package()
        self.assertEqual(self.output.read_bytes(), previous)
        self.assertEqual(list(self.output.parent.iterdir()), [self.output])

    def test_reject_nonportable_or_external_references(self):
        path = self.root / "output/trees.instances.json"
        document = json.loads(path.read_text())
        for value in (str(self.tree), "C:/assets/tree.glb", "..\\assets\\tree.glb", "../../outside.glb"):
            with self.subTest(value=value):
                document["assets"]["tree"]["path"] = value
                self.json("output/trees.instances.json", document)
                with self.assertRaises(ValueError):
                    self.package()
        self.write("output/town.glb", glb({"buffers": [{"uri": "external.bin"}]}))
        with self.assertRaisesRegex(ValueError, "self-contained"):
            self.package()


    def test_default_rejects_standalone_tuned2_and_missing_osm_forests(self):
        self.package()
        previous = self.output.read_bytes()
        path = self.root / "output/trees.instances.json"
        document = json.loads(path.read_text())
        document['source']['default_preparation'] = 'tuned2'
        path.write_text(json.dumps(document))
        with self.assertRaisesRegex(ValueError, 'combined OSM'):
            self.package()
        document['source']['default_preparation'] = 'osm-tuned2'
        path.write_text(json.dumps(document))
        (self.root/'output/osm/forest_patches.json').unlink()
        with self.assertRaisesRegex(ValueError, 'Missing runtime asset'):
            self.package()
        self.assertEqual(previous, self.output.read_bytes())

    def test_each_preset_packages_only_its_selected_layers(self):
        import shutil
        shutil.copy2(self.root/'output/trees.instances.json', self.root/'output/trees.original.instances.json')
        osm = json.loads((self.root/'output/trees.instances.json').read_text())
        osm['assets']['tree']['path'] = '../../assets/tree.glb'
        self.json('output/osm/trees.instances.json', osm)
        shutil.copy2(self.root/'forest_patches.json', self.root/'forest_patches_astra.json')
        expectations = {
            'orthophoto': ('output/trees.original.instances.json', 'forest_patches.json'),
            'osm': ('output/osm/trees.instances.json', 'output/osm/forest_patches.json'),
            'sentinel-astra': (None, 'forest_patches_astra.json'),
            'none': (None, None)}
        all_layers = {'output/trees.instances.json', 'output/trees.original.instances.json',
                      'output/osm/trees.instances.json', 'forest_patches.json',
                      'output/osm/forest_patches.json', 'forest_patches_astra.json'}
        for preset, expected in expectations.items():
            with self.subTest(preset=preset):
                self.package(vegetation=preset)
                with zipfile.ZipFile(self.output) as archive:
                    self.assertEqual(set(archive.namelist()) & all_layers, {p for p in expected if p})
                    self.assertEqual(json.loads(archive.read('asset-pack.json'))['vegetation'], preset)

    def test_disabled_layers_do_not_require_their_assets(self):
        (self.root / "assets/side.png").unlink()
        self.tree.unlink()
        package_assets(self.root, self.map, self.output, None, trees=False)
        with zipfile.ZipFile(self.output) as archive:
            names = archive.namelist()
            self.assertNotIn("output/trees.instances.json", names)
            self.assertNotIn("forest_patches.json", names)
            self.assertFalse(any(name.startswith("assets/zombies/") for name in names))


if __name__ == "__main__":
    unittest.main()
