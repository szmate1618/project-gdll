#!/usr/bin/env python3
"""Generate tuned2 Sentinel tree proposals outside the supplied forest exclusions."""
from __future__ import annotations

import argparse
import json
from pathlib import Path
import sys

from rasterio.errors import RasterioError

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0,str(ROOT))

from src.satellite_tree_tuned2 import prepare_tuned2_trees


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--forest-exclusions",required=True,type=Path,
                        help="Forest JSON controlling exclusions; must match the published map/frame")
    parser.add_argument("--config",type=Path,default=ROOT/"config.json",dest="config_path")
    parser.add_argument("--map",type=Path,default=ROOT/"output/godollo.glb",dest="map_path")
    parser.add_argument("--image",type=Path,dest="image_path",help="Recorded native Sentinel RGB GeoTIFF")
    parser.add_argument("--output-dir",type=Path,help="Separate destination; defaults to satellite_trees/sentinel-astra-tuned2 beside map")
    parser.add_argument("--tree-asset",type=Path,dest="asset_path")
    parser.add_argument("--colour-reference-forests",type=Path,dest="training_forest_path",
                        help="Astra colour-reference JSON; defaults to forest_patches_astra.json and must match calibration")
    parser.add_argument("--recall-profile",type=Path,dest="recall_profile_path")
    parser.add_argument("--precision-profile",type=Path,dest="precision_profile_path")
    args = vars(parser.parse_args(argv))
    exclusions = args.pop("forest_exclusions")
    try:
        print(json.dumps(prepare_tuned2_trees(exclusions,**args),indent=2))
    except (OSError,ValueError,KeyError,RasterioError) as error:
        parser.exit(1,f"Tuned2 tree detection failed: {error}\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
