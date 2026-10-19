#!/usr/bin/env python3
"""Use 10% of default individual trees as stable centers of 15 m pine clusters."""
import argparse
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.tree_preparation import publish_default_trees


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb")
    args = parser.parse_args()
    try:
        result = publish_default_trees(args.map.parent / "trees.instances.json", args.map)
    except (OSError, ValueError, KeyError) as error:
        parser.exit(1, f"Pine assignment failed: {error}\n")
    print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
