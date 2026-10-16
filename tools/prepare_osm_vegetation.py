#!/usr/bin/env python3
"""Prepare separate OSM forests and individual trees for --vegetation osm."""
from __future__ import annotations

import argparse
import json
import logging
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.data import SourceError
from src.osm_vegetation_placements import prepare_osm_vegetation


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json", dest="config_path")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("--input", type=Path, dest="input_path", help="Use a local OSM JSON extract")
    parser.add_argument("--output-dir", type=Path, help="Default: osm/ beside the map")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--offline", action="store_true", help="Require cached OSM inputs")
    mode.add_argument("--refresh", action="store_true", help="Download a fresh OSM extract")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        print(json.dumps(prepare_osm_vegetation(**vars(args)), indent=2))
    except (OSError, ValueError, SourceError) as error:
        print(f"OSM vegetation preparation failed: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
