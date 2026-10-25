#!/usr/bin/env python3
"""Package prepared scene content to extract beside an Actions application build."""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path, PureWindowsPath
import struct
import sys
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.asset_credits import ASSET_CREDIT_FILES, SOFTWARE_CREDIT_FILES, map_credits, vegetation_credits
from src.vegetation_selection import VEGETATION_PRESETS, vegetation_paths
from src.asset_pack_runtime import collect_navigation, pack_instructions


def sha256(path):
    digest = hashlib.sha256()
    with path.open("rb") as stream:
        while chunk := stream.read(1024 * 1024):
            digest.update(chunk)
    return digest.hexdigest()


class RuntimeAssets:
    def __init__(self, root):
        self.root = root.resolve()
        self.files = {}
        self.names = {}
        self.generated = {}

    def add(self, path, expected=None):
        path = path.resolve()
        name = path.relative_to(self.root).as_posix()
        if not path.is_file():
            raise ValueError(f"Missing runtime asset: {path}")
        if self.names.get(name.casefold(), name) != name:
            raise ValueError(f"Asset names collide on Windows: {name}")
        self.names[name.casefold()] = name
        if path not in self.files:
            self.files[path] = sha256(path)
        if expected is not None and self.files[path] != expected:
            raise ValueError(f"Stale fingerprint for {name}; restore or prepare matching assets")
        return path

    def reference(self, parent, value):
        if not isinstance(value, str) or not value or "\\" in value or PureWindowsPath(value).drive or value.startswith("/"):
            raise ValueError(f"Runtime references must be portable relative paths: {value!r}")
        path = parent / value
        # Resolving symlinks to a different archive name would break the reference.
        if Path(os.path.abspath(path)) != path.resolve():
            raise ValueError(f"Runtime asset reference uses a symlink: {value}")
        path.resolve().relative_to(self.root)
        return path

    def document(self, path):
        path = self.add(path)
        with path.open(encoding="utf-8") as stream:
            return json.load(stream)

    def model(self, path, expected=None):
        path = self.add(path, expected)
        if path.suffix != ".glb":
            raise ValueError(f"Expected a self-contained .glb asset: {path}")
        with path.open("rb") as stream:
            header = stream.read(20)
            if len(header) != 20:
                raise ValueError(f"Truncated GLB: {path}")
            magic, version, size, length, kind = struct.unpack("<4sIII4s", header)
            if magic != b"glTF" or version != 2 or size != path.stat().st_size or kind != b"JSON" or length > size - 20:
                raise ValueError(f"Expected GLB 2.0 with a JSON chunk: {path}")
            document = json.loads(stream.read(length))
        for key in ("buffers", "images"):
            for record in document.get(key, []):
                uri = record.get("uri", "")
                if uri and not uri.startswith("data:"):
                    raise ValueError(f"Packager requires self-contained GLBs; external {key} reference in {path}: {uri}")
        return document


def collect_assets(root, map_path, zombies, *, trees=True, forests=True, vegetation="osm-tuned2"):
    assets = RuntimeAssets(root)
    document = assets.model(map_path)
    map_hash = assets.files[map_path.resolve()]
    assets.generated["credits/assets/MAP.txt"] = map_credits(
        document, map_path.relative_to(root).as_posix(), map_hash)
    adjacent = map_path.parent
    tree_path, forest_path = vegetation_paths(root, map_path, vegetation)
    vegetation_layers = []
    placements = [adjacent / "poles.instances.json"]
    if trees and tree_path is not None:
        placements.append(tree_path)
    for path in placements:
        if path != tree_path and not path.exists():
            continue
        document = assets.document(path)
        if path == tree_path:
            vegetation_layers.append((path.relative_to(root).as_posix(), document, assets.files[path.resolve()]))
        if (path == tree_path and vegetation == "osm-tuned2"
                and document.get("source", {}).get("default_preparation") != "osm-tuned2"):
            raise ValueError("Default trees are not the combined OSM+tuned2 layer; run tools/prepare_trees.py")
        if document["map"]["map_sha256"] != map_hash:
            raise ValueError(f"Stale map fingerprint in {path}")
        for record in document.get("assets", {}).values():
            assets.model(assets.reference(path.parent, record["path"]), record["sha256"])
    path = adjacent / "guardrails.glb"
    if path.exists():
        document = assets.model(path)
        if document["scenes"][document.get("scene", 0)]["extras"]["map_sha256"] != map_hash:
            raise ValueError(f"Stale map fingerprint in {path}")
    path = forest_path
    if trees and forests and path is not None:
        document = assets.document(path)
        vegetation_layers.append((path.relative_to(root).as_posix(), document, assets.files[path.resolve()]))
        if document["source"]["map_sha256"] != map_hash:
            raise ValueError(f"Stale map fingerprint in {path}")
        settings = document.get("runtime", {})
        tree_asset = assets.reference(path.parent, settings.get("tree_asset", "assets/tree_impostor.glb"))
        assets.model(tree_asset)
        dry_tree = (assets.reference(path.parent, settings["dry_tree_asset"])
                    if "dry_tree_asset" in settings else tree_asset.parent / "dry_tree_impostor.glb")
        assets.model(dry_tree)
        for key, default in (("side_texture", "assets/forests/side.png"), ("top_texture", "assets/forests/top.png")):
            assets.add(assets.reference(path.parent, settings.get(key, default)))
        side = assets.reference(path.parent, settings.get("side_texture", "assets/forests/side.png"))
        shrub_key = "shrub_texture" if "shrub_texture" in settings else "bush_texture"
        shrub = (assets.reference(path.parent, settings[shrub_key])
                 if shrub_key in settings else side.parent / "shrub.png")
        # Mirror the loader's legacy atlas-name fallback without rewriting config bytes.
        alternate = {"shrub.png": "bush.png", "bush.png": "shrub.png"}.get(shrub.name)
        if not shrub.exists() and alternate and shrub.with_name(alternate).is_file():
            shrub = shrub.with_name(alternate)
        assets.add(shrub)
    assets.generated["credits/assets/VEGETATION.txt"] = vegetation_credits(
        vegetation if trees else "none", vegetation_layers)
    if zombies is not None:
        models = sorted(zombies.glob("*.glb")) if zombies.is_dir() else [zombies]
        if not models or (zombies.is_dir() and list(zombies.glob("*.gltf"))):
            raise ValueError(f"Expected prepared GLB zombie models in {zombies}")
        manifest_path = (zombies if zombies.is_dir() else zombies.parent) / "lods.json"
        manifest = assets.document(manifest_path) if manifest_path.exists() else None
        if manifest is not None and manifest["version"] != 1:
            raise ValueError(f"Unsupported zombie LoD manifest: {manifest_path}")
        for model in models:
            assets.model(model)
            capsules = model.with_suffix(".capsules.json")
            if capsules.exists():
                body_hits = assets.document(capsules)
                if body_hits.get("schema") != "godollo.zombie_bone_capsules" or body_hits.get("version") != 1:
                    raise ValueError(f"Unsupported zombie capsule manifest: {capsules}")
                assets.add(model, body_hits["source_sha256"])
            hitboxes = model.with_suffix(".hitboxes.json")
            if hitboxes.exists():
                hits = assets.document(hitboxes)
                if hits.get("schema") != "godollo.zombie_joint_hits" or hits.get("version") != 1:
                    raise ValueError(f"Unsupported zombie joint manifest: {hitboxes}")
                assets.add(model, hits["source_sha256"])
                for joint in hits["joints"]:
                    mesh = assets.reference(model.parent, joint["mesh"])
                    mesh.resolve().relative_to(model.parent.resolve())
                    assets.model(mesh, joint["mesh_sha256"])
            entry = manifest["models"].get(model.name) if manifest else None
            if entry is None:
                continue
            assets.add(model, entry["source_sha256"])
            if len(entry["levels"]) != 3:
                raise ValueError(f"Expected three zombie LoDs for {model}")
            for level in entry["levels"]:
                lod = assets.reference(model.parent, level["path"])
                lod.resolve().relative_to(model.parent.resolve())
                assets.model(lod, level["sha256"])
    # Visual assets are optional, with the same safe prototype fallback as the
    # client. Include only the current publication, never source archives or old LoDs.
    vehicle_models = root / "assets/vehicles/models"
    vehicle_manifest = vehicle_models / "lods.json"
    if vehicle_manifest.exists():
        manifest = assets.document(vehicle_manifest)
        if manifest["version"] != 2:
            raise ValueError("Unsupported vehicle LoD manifest")
        for entry in manifest["models"].values():
            source = assets.reference(vehicle_models, entry["source"])
            source.resolve().relative_to(vehicle_models.resolve())
            assets.model(source, entry["source_sha256"])
            if len(entry["levels"]) != 4:
                raise ValueError("Expected four vehicle LoDs")
            for level in entry["levels"]:
                path = assets.reference(vehicle_models, level["path"])
                path.resolve().relative_to(vehicle_models.resolve())
                assets.model(path, level["sha256"])
        assets.add(root / "assets/vehicles/CREDITS.md")
        assets.add(root / "assets/vehicles/sources.json")
    car_placements = adjacent / "abandoned_cars.instances.json"
    if car_placements.exists():
        document = assets.document(car_placements)
        if document.get("schema") != "godollo.abandoned-car-placements" or document.get("version") != 1:
            raise ValueError("Unsupported vehicle placement schema/version")
        if document["map"]["map_sha256"] != map_hash:
            raise ValueError("Vehicle placement map fingerprint mismatch")
    weapon_definitions = root / "storage/weapons"
    for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"):
        for suffix in ("gameplay.toml", "inventory.toml", "tags.txt"):
            assets.add(weapon_definitions / f"{name}.{suffix}")
    definitions = sorted([*weapon_definitions.glob("*.toml"), *weapon_definitions.glob("*.tags.txt")])
    ammo_definitions = root / "storage/ammo"
    for name in ("pistol_rounds", "smg_rounds", "sniper_rounds", "shotgun_shells", "he_grenades"):
        for suffix in ("inventory.toml", "tags.txt"):
            assets.add(ammo_definitions / f"{name}.{suffix}")
    definitions += sorted([*ammo_definitions.glob("*.toml"), *ammo_definitions.glob("*.tags.txt")])
    vehicles = sorted((root / "vehicles").glob("*.toml"))
    if not vehicles:
        raise ValueError("Vehicle configuration files are missing")
    storage = [root / "storage" / name for name in ("equipment.toml", "backpacks.toml", "loadout.toml")]
    skills = [root / "skills.toml", *sorted((root / "skills/melee").glob("*.skill.toml"))]
    for path in [*definitions, *vehicles, *storage, *skills, root / "inventory.toml"]:
        assets.add(path)
    for name in ("assets/README.md", "assets/zombies/README.md", "assets/zombies/sources.json", "assets/forests/README.md"):
        if (root / name).exists():
            assets.add(root / name)
    for name in ASSET_CREDIT_FILES:
        assets.add(root / "credits/assets" / name)
    for name in SOFTWARE_CREDIT_FILES:
        assets.add(root / "credits/software" / name)
    # Preserve supplied notices for custom maps as well as the authoritative
    # metadata-derived MAP.txt. The sidecar never supplies its acquisition year.
    if (adjacent / "ATTRIBUTION.md").exists():
        assets.add(adjacent / "ATTRIBUTION.md")
    return assets


def package_assets(root, map_path, output, zombies, *, trees=True, forests=True, vegetation="osm-tuned2"):
    root, map_path, output = root.resolve(), map_path.resolve(), output.resolve()
    zombies = zombies.resolve() if zombies is not None else None
    if output.suffix.lower() != ".zip":
        raise ValueError("Output must be a .zip file")
    assets = collect_assets(root, map_path, zombies, trees=trees, forests=forests, vegetation=vegetation)
    navigation = collect_navigation(assets, map_path)
    scenarios = [assets.add(path).relative_to(root).as_posix()
                 for path in sorted((root / "scenarios").rglob("scenario.toml"))]
    launch, generated = pack_instructions(map_path.relative_to(root).as_posix(),
                                        zombies.relative_to(root).as_posix() if zombies else None,
                                        vegetation, trees, forests)
    assets.generated.update(generated)
    manifest = {"version": 1, "map": map_path.relative_to(root).as_posix(),
                "vegetation": vegetation if trees else "none",
                "trees": trees and vegetation not in ("none", "sentinel-astra"),
                "forests": trees and forests and vegetation != "none", "zombies": zombies.relative_to(root).as_posix() if zombies else None,
                "navigation": navigation, "scenarios": scenarios, "launch": launch,
                "files": {p.relative_to(root).as_posix(): digest for p, digest in sorted(assets.files.items())}}
    manifest["files"].update({name: hashlib.sha256(data).hexdigest() for name, data in assets.generated.items()})
    output.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(prefix=output.stem + "-", suffix=".zip", dir=output.parent, delete=False) as stream:
        temporary = Path(stream.name)
    try:
        total_size = 0
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as archive:
            for path, expected in sorted(assets.files.items()):
                digest = hashlib.sha256()
                with path.open("rb") as source, archive.open(path.relative_to(root).as_posix(), "w", force_zip64=True) as target:
                    while chunk := source.read(1024 * 1024):
                        target.write(chunk)
                        digest.update(chunk)
                        total_size += len(chunk)
                if digest.hexdigest() != expected:
                    raise ValueError(f"Asset changed during packaging: {path}")
            for name, data in assets.generated.items():
                info = zipfile.ZipInfo(name)
                info.create_system = 3
                info.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
                info.compress_type = zipfile.ZIP_DEFLATED
                archive.writestr(info, data)
                total_size += len(data)
            archive.writestr("asset-pack.json", json.dumps(manifest, indent=2) + "\n")
        with zipfile.ZipFile(temporary) as archive:
            corrupt = archive.testzip()
            if corrupt:
                raise ValueError(f"Archive verification failed: {corrupt}")
        os.replace(temporary, output)
    finally:
        temporary.unlink(missing_ok=True)
    return len(manifest["files"]), total_size


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb")
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "build/godollo-assets.zip")
    parser.add_argument("--zombies", type=Path, default=ROOT / "assets/zombies/models")
    parser.add_argument("--no-zombies", action="store_true", help="For sessions using --zombie-count 0")
    parser.add_argument("--vegetation", choices=VEGETATION_PRESETS, default="osm-tuned2",
                        help="Preset to package; default: OSM forests, trees/rows and tuned2")
    parser.add_argument("--no-trees", action="store_true", help="Omit detected trees and runtime forests")
    parser.add_argument("--no-forests", action="store_true")
    args = parser.parse_args(argv)
    try:
        count, size = package_assets(ROOT, args.map, args.output, None if args.no_zombies else args.zombies.resolve(),
                                    trees=not args.no_trees, forests=not args.no_forests, vegetation=args.vegetation)
    except (OSError, ValueError, KeyError, TypeError, zipfile.BadZipFile) as error:
        print(f"Asset packaging failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {count} runtime files: {size / 1e6:.1f} MB unpacked, {args.output.stat().st_size / 1e6:.1f} MB ZIP")
    print(args.output.resolve())
    print("Extract the Actions game package, then this ZIP into the same game folder.\n"
          "Start play-town.cmd on Windows or ./play-town.sh on Linux.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
