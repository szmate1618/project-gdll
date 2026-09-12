#!/usr/bin/env python3
"""Filter detector CSVs to config.json and prepare terrain-grounded tree instances."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.tree_placements import prepare_instances
from pyproj.exceptions import ProjError


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", nargs="?", type=Path, default=ROOT / "data/processed/tree_detections.csv")
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "output/trees.instances.json")
    parser.add_argument("--config", type=Path, default=ROOT / "config.json")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("--image-root", type=Path, action="append", default=[],
                        help="Directory searched recursively for referenced TIFFs; repeat for multiple roots")
    parser.add_argument("--source-crs", help="Explicitly override TIFF CRS (e.g. EPSG:23700 for Hungarian EOV)")
    parser.add_argument("--species", choices=("tree", "pine"), default="tree")
    parser.add_argument("--height-m", type=float, help="Visible tree height in meters; default is the asset's height")
    parser.add_argument("--min-score", type=float, default=0, help="Minimum confidence, 0..1 (default keeps all scores)")
    args = parser.parse_args(argv)
    try:
        result = prepare_instances(args.input, args.output, config_path=args.config, map_path=args.map_path,
                                   image_roots=args.image_root, source_crs=args.source_crs, species=args.species,
                                   height_m=args.height_m, min_score=args.min_score)
    except (ValueError, OSError, ProjError) as error:
        print(f"Tree preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {result['instances']:,} tree instances to {result['output']} "
          f"({result['input']:,} detections; {result['outside_area']:,} outside area; "
          f"{result['below_min_score']:,} below score; {result['rasters']} source TIFFs)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
