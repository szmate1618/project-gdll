#!/usr/bin/env python3
"""Change only road elevation in an existing map, with validated backups."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.config import load_config
from src.road_scene_update import update_road_offset


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("--from-offset", type=float, required=True,
                        help="Existing offset in metres; target comes from config roads.offset_m")
    parser.add_argument("--companion", type=Path, action="append", default=[],
                        help="Refresh a matching tree/pole/forest JSON or guardrail GLB fingerprint")
    args = parser.parse_args()
    result = update_road_offset(args.map_path, load_config(args.config), args.from_offset, args.companion)
    print(f"Road offset: {result['offset_m']} m; originals: {result['backup']}")


if __name__ == "__main__":
    main()
