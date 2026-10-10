#!/usr/bin/env python3
"""Apply track texture to an existing published map using cached OSM only."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.config import load_config
from src.railway_texture_apply import apply_railway_texture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb")
    parser.add_argument("--config", type=Path, default=ROOT / "config.json")
    args = parser.parse_args()
    print(json.dumps(apply_railway_texture(args.map, load_config(args.config)), indent=2))


if __name__ == "__main__":
    main()
