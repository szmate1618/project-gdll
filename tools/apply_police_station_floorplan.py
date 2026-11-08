#!/usr/bin/env python3
"""Apply the fictional station interior to an existing map without town regeneration."""
import argparse
import json
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from src.config import load_config
from src.police_station_scene_update import update_station


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--map', type=Path, default=Path('output/godollo.glb'))
    parser.add_argument('--config', type=Path, default=Path('config.json'))
    args = parser.parse_args()
    companions = [args.map.parent / name for name in (
        'trees.instances.json', 'poles.instances.json', 'abandoned_cars.instances.json',
        'guardrails.glb', 'osm/trees.instances.json')]
    print(json.dumps(update_station(args.map, load_config(args.config),
                                    [p for p in companions if p.exists()]), indent=2))


if __name__ == '__main__':
    main()
