"""Collect the three prepared dog appearances and their current LoDs only."""
from __future__ import annotations


def collect_dogs(assets, directory):
    names = {f"undead_dog_variant_{variant}.glb" for variant in range(1, 4)}
    manifest_path = directory / "lods.json"
    manifest = assets.document(manifest_path)
    if manifest["version"] != 1 or set(manifest["models"]) != names:
        raise ValueError(f"Expected a version 1 LoD manifest for the three dog variants: {manifest_path}")
    for name in sorted(names):
        model = assets.reference(directory, name)
        entry = manifest["models"][name]
        assets.model(model, entry["source_sha256"])
        if len(entry["levels"]) != 3:
            raise ValueError(f"Expected three dog LoDs for {model}")
        for level in entry["levels"]:
            lod = assets.reference(directory, level["path"])
            lod.resolve().relative_to(directory.resolve())
            assets.model(lod, level["sha256"])
    # Provenance and notices accompany game-ready models, never source archives,
    # texture comparisons, previews, Blender scenes or obsolete hashed LoDs.
    for name in ("README.md", "sources.json", "animations/run-source.json"):
        assets.add(assets.root / "assets/undead_dog" / name)
