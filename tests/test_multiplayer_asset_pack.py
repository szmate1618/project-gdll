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
from src.asset_credits import ASSET_CREDIT_FILES


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
        self.zombie = self.write("assets/zombies/models/Zombie.glb", glb({}))
        self.write("assets/zombies/models/unused-old-lod.glb.bak", b"not runtime")
        levels = []
        for level in range(1, 4):
            p = self.write(f"assets/zombies/models/lod{level}/Zombie.glb", glb({"extras": {"level": level}}))
            levels.append({"path": f"lod{level}/Zombie.glb", "sha256": self.digest(p)})
        self.json("assets/zombies/models/lods.json", {"version": 1, "models": {
            "Zombie.glb": {"source_sha256": self.digest(self.zombie), "levels": levels}}})
        self.json("output/trees.instances.json", {"map": {"map_sha256": self.digest(self.map)}, "assets": {
            "tree": {"path": "../assets/tree.glb", "sha256": self.digest(self.tree)}}, "instances": []})
        self.json("output/poles.instances.json", {"map": {"map_sha256": self.digest(self.map)}})
        self.write("output/guardrails.glb", glb({"scenes": [{"extras": {"map_sha256": self.digest(self.map)}}]}))
        self.json("forest_patches.json", {"source": {"map_sha256": self.digest(self.map)}, "runtime": {
            "tree_asset": "assets/tree.glb", "side_texture": "assets/side.png", "top_texture": "assets/top.png"}})
        self.write("assets/side.png", b"side texture")
        self.write("assets/top.png", b"top texture")
        self.write("inventory.toml", b"inventory\n")
        for name in ("pistol", "submachine_gun", "sniper", "shotgun"):
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
        expected = {"output/town.glb", "output/ATTRIBUTION.md", "output/trees.instances.json", "output/poles.instances.json", "output/guardrails.glb",
                    "assets/tree.glb", "forest_patches.json", "assets/side.png", "assets/top.png", "inventory.toml",
                    "assets/zombies/models/Zombie.glb", "assets/zombies/models/lods.json"}
        expected.update(f"items/{name}.toml" for name in ("pistol", "submachine_gun", "sniper", "shotgun"))
        expected.update(f"assets/zombies/models/lod{level}/Zombie.glb" for level in range(1, 4))
        expected.update(f"credits/assets/{name}" for name in (*ASSET_CREDIT_FILES, "MAP.txt"))
        self.assertEqual(set(manifest["files"]), expected)
        for name, digest in manifest["files"].items():
            self.assertNotIn("\\", name)
            self.assertFalse(Path(name).is_absolute())
            if name != "credits/assets/MAP.txt":
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

    def test_missing_credit_preserves_previous_archive(self):
        self.package()
        previous = self.output.read_bytes()
        (self.root / "credits/assets/SENTINEL.txt").unlink()
        with self.assertRaisesRegex(ValueError, "Missing runtime asset"):
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
