#!/usr/bin/env python3
"""Prepare default tuned2 trees with OSM forest exclusions for an existing map."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from rasterio.errors import RasterioError

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.tree_preparation import prepare_trees
from src.data import SourceError


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json", dest="config_path")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("--forest-exclusions", type=Path,
                        help="Defaults to osm/forest_patches.json beside the map")
    parser.add_argument("--output-dir", type=Path,
                        help="Detector artifacts; defaults to satellite_trees/tuned2-osm beside the map")
    parser.add_argument("--image", type=Path, dest="image_path")
    parser.add_argument("--tree-asset", type=Path, dest="asset_path")
    parser.add_argument("--colour-reference-forests", type=Path, dest="training_forest_path")
    parser.add_argument("--recall-profile", type=Path, dest="recall_profile_path")
    parser.add_argument("--precision-profile", type=Path, dest="precision_profile_path")
    parser.add_argument("--refresh", action="store_true", help="Refresh the OSM exclusion extract")
    parser.add_argument("--online", action="store_false", dest="offline", default=True,
                        help="Allow fetching missing OSM inputs (default: cached inputs only)")
    args = vars(parser.parse_args(argv))
    if args["refresh"]:
        args["offline"] = False
    try:
        print(json.dumps(prepare_trees(**args), indent=2))
    except (OSError, ValueError, KeyError, RasterioError, SourceError) as error:
        parser.exit(1, f"Default tree preparation failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
