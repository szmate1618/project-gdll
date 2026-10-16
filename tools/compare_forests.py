#!/usr/bin/env python3
"""Compare original, OSM and Sentinel-Astra forests over satellite imagery."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.forest_comparison_report import create_comparison


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--old", type=Path, default=ROOT / "forest_patches.json")
    parser.add_argument("--new", type=Path, default=ROOT / "forest_patches_astra.json")
    parser.add_argument("--osm", type=Path, default=ROOT / "output/osm/forest_patches.json")
    parser.add_argument("--osm-trees", type=Path, help="Defaults to trees.instances.json beside the OSM forests")
    parser.add_argument("--imagery", type=Path,
                        help="Defaults to the new config's source satellite raster, else processed Sentinel RGB")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb")
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "output/forest_astra_comparison")
    args = parser.parse_args(argv)
    try:
        if args.imagery is None:
            document = json.loads(args.new.read_text(encoding="utf-8"))
            raster = document.get("source", {}).get("satellite", {}).get("path")
            args.imagery = (args.new.parent / raster if raster else ROOT / "data/processed/sentinel_rgb.tif")
        if args.osm_trees is None:
            adjacent = args.osm.parent / "trees.instances.json"
            args.osm_trees = adjacent if adjacent.exists() else None
        report = create_comparison(args.old, args.new, args.imagery, args.output, args.map,
                                   args.osm, args.osm_trees)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Forest comparison failed: {error}\n")
    print(json.dumps(report["datasets"], indent=2))
    print(f"Comparison: {args.output / 'index.html'}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
