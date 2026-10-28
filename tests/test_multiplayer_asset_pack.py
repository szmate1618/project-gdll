"""Portable asset exports preserve fingerprints and exclude preparation inputs."""
import hashlib
import json
import os
from pathlib import Path
import shutil
import struct
import subprocess
import tempfile
import unittest
from unittest.mock import patch
import zipfile

from tools.package_multiplayer_assets import package_assets
from src.asset_credits import ASSET_CREDIT_FILES, SOFTWARE_CREDIT_FILES
from src.asset_pack_runtime import pack_instructions


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
        self.write("skills.toml", b"[melee]\npunch='Punch'\n")
        self.write("skills/melee/punch.skill.toml", b"")
        for name in ("equipment.toml", "backpacks.toml", "loadout.toml"):
            self.write(f"storage/{name}", b"storage config\n")
        self.write("vehicles/car.toml", b"vehicle config\n")
        for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"):
            self.write(f"storage/weapons/{name}.inventory.toml", b"item definition\n")
            self.write(f"storage/weapons/{name}.gameplay.toml", b"weapon gameplay\n")
            self.write(f"storage/weapons/{name}.tags.txt", b"long_gun, ranged\n")
        for name in ("pistol_rounds", "smg_rounds", "sniper_rounds", "shotgun_shells", "he_grenades"):
            self.write(f"storage/ammo/{name}.inventory.toml", b"ammo item definition\n")
            self.write(f"storage/ammo/{name}.tags.txt", b"ammo, stackable\n")
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

    def test_joint_hit_resources_and_fingerprints(self):
        mesh = self.write("assets/zombies/models/severed/elbow.glb", glb({"extras": {"severed": True}}))
        hits = {"schema": "godollo.zombie_joint_hits", "version": 1,
                "source_sha256": self.digest(self.zombie), "joints": [
                    {"mesh": "severed/elbow.glb", "mesh_sha256": self.digest(mesh)}]}
        self.json("assets/zombies/models/Zombie.hitboxes.json", hits)
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn("assets/zombies/models/Zombie.hitboxes.json", archive.namelist())
            self.assertEqual(archive.read("assets/zombies/models/severed/elbow.glb"), mesh.read_bytes())
        mesh.write_bytes(glb({"extras": {"changed": True}}))
        with self.assertRaisesRegex(ValueError, "Stale fingerprint"):
            self.package()

    def test_bone_capsule_resources_and_fingerprints(self):
        path = self.json("assets/zombies/models/Zombie.capsules.json", {
            "schema": "godollo.zombie_bone_capsules", "version": 1,
            "source_sha256": self.digest(self.zombie), "bones": []})
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read("assets/zombies/models/Zombie.capsules.json"), path.read_bytes())
        self.zombie.write_bytes(glb({"extras": {"changed": True}}))
        with self.assertRaisesRegex(ValueError, "Stale fingerprint"):
            self.package()

    def test_car_positions_are_packaged_with_the_matching_map(self):
        path = self.root/'output/abandoned_cars.instances.json'
        self.json('output/abandoned_cars.instances.json',{'schema':'godollo.abandoned-car-placements',
            'version':1,'map':{'map_sha256':self.digest(self.map)},'instances':[]})
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertEqual(archive.read('output/abandoned_cars.instances.json'),path.read_bytes())
        previous = self.output.read_bytes()
        data = json.loads(path.read_text());data['map']['map_sha256']='stale'
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(ValueError,'fingerprint'):
            self.package()
        self.assertEqual(self.output.read_bytes(),previous)

    def vehicle_publication(self):
        source = self.write('assets/vehicles/models/car.glb',glb({}))
        levels = []
        for level in range(4):
            p = self.write(f'assets/vehicles/models/lod{level}/car.glb',glb({}))
            levels.append({'path':f'lod{level}/car.glb','sha256':self.digest(p)})
        self.write('assets/vehicles/CREDITS.md',b'CC BY attribution')
        self.json('assets/vehicles/sources.json',{'license':'CC BY 4.0'})
        self.json('assets/vehicles/models/lods.json',{'version':2,'models':{'car':{
            'source':'car.glb','source_sha256':self.digest(source),'levels':levels}}})
        return source

    def test_vehicle_pack_includes_current_lods_and_credits(self):
        self.vehicle_publication()
        self.write('assets/vehicles/models/lod0/old.glb',b'unused stale cache')
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            self.assertIn('assets/vehicles/CREDITS.md',archive.namelist())
            self.assertIn('assets/vehicles/models/car.glb',archive.namelist())
            for level in range(4):
                self.assertIn(f'assets/vehicles/models/lod{level}/car.glb',archive.namelist())
            self.assertNotIn('assets/vehicles/models/lod0/old.glb',archive.namelist())

    def test_stale_vehicle_cache_keeps_previous_package(self):
        source = self.vehicle_publication()
        self.package()
        previous = self.output.read_bytes()
        source.write_bytes(glb({'extras':{'changed':True}}))
        with self.assertRaisesRegex(ValueError,'fingerprint'):
            self.package()
        self.assertEqual(self.output.read_bytes(),previous)

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
        expected.add("vehicles/car.toml")
        expected.update({"skills.toml", "skills/melee/punch.skill.toml", "storage/equipment.toml", "storage/backpacks.toml", "storage/loadout.toml"})
        for suffix in ("inventory.toml", "gameplay.toml", "tags.txt"):
            expected.update(f"storage/weapons/{name}.{suffix}" for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"))
        for suffix in ("inventory.toml", "tags.txt"):
            expected.update(f"storage/ammo/{name}.{suffix}" for name in ("pistol_rounds", "smg_rounds", "sniper_rounds", "shotgun_shells", "he_grenades"))
        expected.update(f"assets/zombies/models/lod{level}/Zombie.glb" for level in range(1, 4))
        expected.update(f"credits/assets/{name}" for name in (*ASSET_CREDIT_FILES, "MAP.txt", "VEGETATION.txt"))
        expected.update(f"credits/software/{name}" for name in SOFTWARE_CREDIT_FILES)
        expected.update({"play-town.cmd", "play-town.sh", "ASSET-PACK-README.txt"})
        self.assertEqual(set(manifest["files"]), expected)
        for name, digest in manifest["files"].items():
            self.assertNotIn("\\", name)
            self.assertFalse(Path(name).is_absolute())
            if name not in ("credits/assets/MAP.txt", "credits/assets/VEGETATION.txt",
                            "play-town.cmd", "play-town.sh", "ASSET-PACK-README.txt"):
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

    def navigation_publication(self):
        replay = self.json("output/town.navigation.json", {"map_sha256": self.digest(self.map)})
        header = bytearray(128)
        struct.pack_into("<8sII8dQQ", header, 0, b"GDNAV02\0", 4, 1,
                         -200, -200, 200, 200, .3, .002, .05, .005, 64, 36)
        header[96:] = bytes.fromhex(self.digest(replay))
        mesh = self.write("output/town.navmesh", header + struct.pack("<8d", -200, -200, 200, -200, 200, 200, -200, 200)
                          + struct.pack("<9I", 4, 0, 1, 2, 3, *([0xffffffff] * 4)))
        metadata = self.json("output/town.navmesh.json", {
            "format": "GDNAV02", "map_sha256": self.digest(self.map), "output_sha256": self.digest(mesh),
            "source": replay.name, "source_sha256": self.digest(replay)})
        return mesh, replay, metadata

    def test_navigation_metadata_and_scenarios_are_included(self):
        paths = self.navigation_publication()
        scenario = self.write("scenarios/example/scenario.toml", b"version = 4\n")
        self.write("scenarios/example/preview.png", b"not needed at runtime")
        self.package()
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read("asset-pack.json"))
            self.assertEqual(manifest["navigation"], "output/town.navmesh")
            self.assertEqual(manifest["scenarios"], ["scenarios/example/scenario.toml"])
            for path in [*paths, scenario]:
                name = path.relative_to(self.root).as_posix()
                self.assertEqual(archive.read(name), path.read_bytes())
            self.assertNotIn("scenarios/example/preview.png", archive.namelist())

    def test_scenario_density_references_are_included_and_missing_image_preserves_archive(self):
        self.write("scenarios/example/scenario.toml", b"version = 5\n[zombies.horde]\ncount = 1\n"
                   b"movement = 'travel'\nspawn_density = '../density/gray_scott_corridors.png'\n"
                   b"destination_density = 'destination.png'\n")
        spawn = self.write("scenarios/density/gray_scott_corridors.png", b"spawn density bytes")
        destination = self.write("scenarios/example/destination.png", b"destination density bytes")
        self.package()
        previous = self.output.read_bytes()
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read("asset-pack.json"))
            for path in (spawn, destination):
                name = path.relative_to(self.root).as_posix()
                self.assertEqual(archive.read(name), path.read_bytes())
                self.assertEqual(manifest["files"][name], self.digest(path))
        spawn.unlink()
        with self.assertRaises(ValueError):
            self.package()
        self.assertEqual(self.output.read_bytes(), previous)

    def test_scenario_density_references_cannot_escape_package_root(self):
        self.write("scenarios/example/scenario.toml", b"version = 5\n[zombies.horde]\ncount = 1\n"
                   b"spawn_density = '../../../outside.png'\n")
        with self.assertRaises(ValueError):
            self.package()

    def test_stale_or_incomplete_navigation_preserves_previous_archive(self):
        mesh, replay, metadata = self.navigation_publication()
        self.package()
        previous = self.output.read_bytes()
        for path in (mesh, replay, metadata):
            original = path.read_bytes()
            path.unlink()
            with self.subTest(missing=path), self.assertRaises(ValueError):
                self.package()
            path.write_bytes(original)
            self.assertEqual(self.output.read_bytes(), previous)
        for field in ("map_sha256", "output_sha256", "source_sha256"):
            original = metadata.read_bytes()
            document = json.loads(original)
            document[field] = "0" * 64
            metadata.write_text(json.dumps(document))
            with self.subTest(stale=field), self.assertRaisesRegex(ValueError, "fingerprint|provenance"):
                self.package()
            metadata.write_bytes(original)
            self.assertEqual(self.output.read_bytes(), previous)

    def test_launchers_apply_the_pack_options_and_windows_line_endings(self):
        self.package(trees=False, forests=False, vegetation="none")
        with zipfile.ZipFile(self.output) as archive:
            manifest = json.loads(archive.read("asset-pack.json"))
            self.assertIn("--no-trees", manifest["launch"]["arguments"])
            self.assertIn("--zombies", manifest["launch"]["arguments"])
            script = archive.read("play-town.cmd")
            self.assertIn(b"town.glb", script)
            self.assertIn(b"%*", script)
            self.assertNotIn(b"\n", script.replace(b"\r\n", b""))
            self.assertTrue((archive.getinfo("play-town.sh").external_attr >> 16) & 0o111)
            self.assertIn(b'"$@"', archive.read("play-town.sh"))
        launch, files = pack_instructions("output/town space%name&more.glb", None, "osm", True, False)
        self.assertIn("--no-forests", launch["arguments"])
        self.assertEqual(launch["arguments"][-2:], ["--zombie-count", "0"])
        self.assertIn(b'"output/town space%%name&more.glb"', files["play-town.cmd"])
        self.assertIn(b"'output/town space%name&more.glb'", files["play-town.sh"])

    @unittest.skipUnless(os.name == "posix", "Exercises the Linux launcher")
    def test_linux_launcher_preserves_paths_and_extra_arguments(self):
        launch, generated = pack_instructions("output/a folder/town.glb", None, "osm", True, False)
        folder = self.root / "game with spaces"
        folder.mkdir()
        script = folder / "play-town.sh"
        script.write_bytes(generated["play-town.sh"])
        client = folder / "run-client.sh"
        client.write_text('#!/bin/sh\nprintf "%s\\n" "$@"\n')
        client.chmod(0o755)
        result = subprocess.run(["sh", str(script), "--host", "--port", "27022"], cwd=self.root,
                                check=True, capture_output=True, text=True)
        self.assertEqual(result.stdout.splitlines(), launch["arguments"] + ["--host", "--port", "27022"])

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
