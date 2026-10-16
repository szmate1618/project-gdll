#!/usr/bin/env python3
"""Rebuild the alternate satellite-only forest configuration from saved traces."""
from __future__ import annotations

import argparse
import copy
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

import numpy as np

from src.config import load_config
from src.forest_review import build_patches
from src.tree_map import load_tree_map


def sha256(path):
    with path.open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--traces", type=Path, default=ROOT / "assets/forests/astra_canopy_traces.json")
    parser.add_argument("--output", type=Path, default=ROOT / "forest_patches_astra.json")
    args = parser.parse_args()
    output = args.output.resolve()
    if output in ((ROOT / "forest_patches.json").resolve(), args.traces.resolve()):
        parser.error("Output must preserve the default forest configuration and source traces")
    trace = json.loads(args.traces.read_text(encoding="utf-8"))
    terrain, frame, metadata = load_tree_map(ROOT / "output/godollo.glb", load_config(ROOT / "config.json"))
    recorded = trace["coordinate_frame"]
    if (recorded["crs"] != frame["crs"] or recorded["units"] != "meters"
            or recorded["coordinate_order"] != ["east", "north"]
            or not np.allclose(recorded["origin_projected_m"], frame["origin"], rtol=0, atol=1e-6)
            or not np.allclose(recorded["bounds_m"], terrain.bounds, rtol=0, atol=.001)):
        raise ValueError("Saved trace frame differs from the published map")
    template = copy.deepcopy(trace["dataset"])
    source = template["source"]
    if source["map_sha256"] != metadata["map_sha256"]:
        raise ValueError("Published map fingerprint changed; review traces before rebuilding")
    satellite = source["satellite"]
    for path, expected in [(satellite["path"], satellite["sha256"]),
                           (satellite["processed_path"], satellite["processed_sha256"]),
                           (source["texture_path"], source["texture_sha256"])]:
        if sha256(ROOT / path) != expected:
            raise ValueError(f"Saved satellite review fingerprint differs: {path}")
    source["astra_review"]["trace_path"] = os.path.relpath(args.traces.resolve(), ROOT)
    source["astra_review"]["trace_sha256"] = sha256(args.traces)
    regions, patches = build_patches(trace)
    for patch in patches:
        for ring in [patch["polygon"], *patch["holes"]]:
            if not all(np.isfinite(terrain(*point)) for point in ring):
                raise ValueError("Forest patch has no published terrain support")
    for key in ("tree_asset", "side_texture", "top_texture"):
        asset = ROOT / template["runtime"][key]
        if not asset.is_file():
            raise ValueError(f"Missing runtime asset: {asset}")
        template["runtime"][key] = os.path.relpath(asset, output.parent)
    config = dict(schema="godollo.forest_patches", version=1, description=template["description"],
                  coordinate_frame=recorded, source=source, subdivision=template["subdivision"],
                  counts=dict(regions=len(regions), patches=len(patches),
                              total_area_m2=round(sum(p["area_m2"] for p in patches), 3)),
                  runtime=template["runtime"], regions=regions, patches=patches)
    encoded = (json.dumps(config, indent=2, ensure_ascii=False) + "\n").encode("utf-8")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=output.parent, prefix=output.name + ".", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
        json.loads(temporary.read_text(encoding="utf-8"))
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    print(json.dumps(config["counts"]))
    print(output)


if __name__ == "__main__":
    main()
