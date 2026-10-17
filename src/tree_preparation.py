"""Prepare and publish the default tuned2 tree layer without rebuilding the town."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import tempfile

from .config import ROOT
from .forest_comparison import file_sha256
from .osm_vegetation_placements import prepare_osm_vegetation
from .satellite_tree_tuned2 import prepare_tuned2_trees
from .tree_combination import combine_trees


def publish_default_trees(placement_path, map_path):
    """Rebase runtime references, preserve the old layer and atomically replace it.

    Backups stay beside the map so their relative map/asset references still work.
    Publication checks the generated layer, map, assets and previous default again
    immediately before replacement. A failed replacement leaves that default intact.
    """
    placement_path, map_path = Path(placement_path).resolve(), Path(map_path).resolve()
    target = map_path.parent / "trees.instances.json"
    if placement_path == target:
        raise ValueError("Prepare tuned2 in a separate directory before publishing the default")
    previous = target.read_bytes() if target.exists() else None
    fingerprints = {placement_path: file_sha256(placement_path), map_path: file_sha256(map_path)}
    document = json.loads(placement_path.read_text(encoding="utf-8"))
    if document.get("schema") != "godollo.tree-instances" or document.get("version") != 1:
        raise ValueError("Unsupported generated tree layer")
    if document["map"]["map_sha256"] != fingerprints[map_path]:
        raise ValueError("Generated trees do not match the published map")
    if (placement_path.parent / document["map"]["path"]).resolve() != map_path:
        raise ValueError("Generated trees reference a different map")
    for asset in document["assets"].values():
        path = (placement_path.parent / asset["path"]).resolve()
        if path == target or path == map_path or file_sha256(path) != asset["sha256"]:
            raise ValueError("Generated trees reference an invalid or stale asset")
        fingerprints[path] = asset["sha256"]
        asset["path"] = os.path.relpath(path, target.parent)
    document["map"]["path"] = os.path.relpath(map_path, target.parent)
    document["source"].setdefault("default_preparation", "tuned2")
    encoded = (json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False) + "\n").encode("utf-8")
    backup = None
    with tempfile.TemporaryDirectory(prefix=".default-trees-", dir=target.parent) as scratch:
        staged = Path(scratch) / target.name
        staged.write_bytes(encoded)
        json.loads(staged.read_text(encoding="utf-8"))
        if previous is not None and previous != encoded:
            backup = target.with_name("trees.original.instances.json")
            if backup.exists() and backup.read_bytes() != previous:
                backup = target.with_name(f"trees.previous.{hashlib.sha256(previous).hexdigest()}.instances.json")
            if backup.exists():
                if backup.read_bytes() != previous:
                    raise ValueError("Refusing to overwrite a differing tree backup")
            else:
                saved = Path(scratch) / backup.name
                saved.write_bytes(previous)
                # Create the complete backup without replacing an existing file.
                os.link(saved, backup)
        for path, expected in fingerprints.items():
            if file_sha256(path) != expected:
                raise ValueError(f"Tree publication input changed: {path}")
        if (target.read_bytes() if target.exists() else None) != previous:
            raise ValueError("Default trees changed during publication; retry")
        os.replace(staged, target)
    return {"output": str(target), "backup": str(backup) if backup else None,
            "instances": len(document["instances"])}


def prepare_trees(*, map_path=ROOT / "output/godollo.glb", forest_exclusions=None,
                  output_dir=None, offline=True, refresh=False, **options):
    """Default workflow: combine OSM trees/rows and tuned2 outside OSM forests."""
    map_path = Path(map_path).resolve()
    exclusions = (Path(forest_exclusions).resolve() if forest_exclusions is not None
                  else map_path.parent / "osm/forest_patches.json")
    output_dir = (Path(output_dir).resolve() if output_dir is not None
                  else map_path.parent / "satellite_trees/tuned2-osm")
    if forest_exclusions is None:
        map_hash = file_sha256(map_path)
        forest = json.loads(exclusions.read_text()) if exclusions.exists() else {}
        if (refresh or forest.get("source", {}).get("map_sha256") != map_hash
                or not (map_path.parent / "osm/trees.instances.json").exists()):
            prepare_osm_vegetation(map_path=map_path, output_dir=exclusions.parent,
                                   config_path=options.get("config_path", ROOT / "config.json"),
                                   root=options.get("root", ROOT), offline=offline, refresh=refresh)
    result = prepare_tuned2_trees(exclusions, map_path=map_path, output_dir=output_dir,
                                 reuse_calibration=True, **options)
    combination = combine_trees(map_path.parent / "osm/trees.instances.json",
                                output_dir / "trees.instances.json", map_path,
                                map_path.parent / "satellite_trees/osm-tuned2/trees.instances.json")
    publication = publish_default_trees(combination["output"], map_path)
    return {**result, "preparation_output": result["output"], **combination, **publication,
            "forest_exclusions": str(exclusions)}
