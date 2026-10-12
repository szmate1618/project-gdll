#!/usr/bin/env python3
"""Add mapped utility columns and approximate HÉV masts without rebuilding the town."""
from __future__ import annotations

import argparse
import logging
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.data import SourceError
from src.pole_placements import prepare_poles


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("-o", "--output", type=Path, help="Defaults to poles.instances.json beside the map")
    parser.add_argument("--offline", action="store_true", help="Require a previously cached pole query")
    parser.add_argument("--refresh", action="store_true", help="Fetch a new pole extract")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        result = prepare_poles(config_path=args.config, map_path=args.map_path, output=args.output,
                               offline=args.offline, refresh=args.refresh)
    except (OSError, ValueError, SourceError) as error:
        print(f"Pole preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {result['columns']} poles to {result['output']}")
    for kind in ("street_lamp", "power_pole", "utility_pole", "power_tower", "hev_catenary"):
        print(f"  {kind}: {result.get(kind, 0)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
