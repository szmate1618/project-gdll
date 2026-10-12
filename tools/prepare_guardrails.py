#!/usr/bin/env python3
"""Add OSM roadside guardrails to an existing town using minimal textured strips."""
from __future__ import annotations

import argparse
import logging
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.data import SourceError
from src.guardrail_scene import prepare_guardrails


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--config", type=Path, default=ROOT / "config.json", dest="config_path")
    parser.add_argument("--map", type=Path, default=ROOT / "output/godollo.glb", dest="map_path")
    parser.add_argument("-o", "--output", type=Path, help="Defaults to guardrails.glb beside the map")
    parser.add_argument("--offline", action="store_true")
    parser.add_argument("--refresh", action="store_true")
    args = parser.parse_args(argv)
    logging.basicConfig(level=logging.INFO, format="%(levelname)s: %(message)s")
    try:
        result = prepare_guardrails(**vars(args))
    except (OSError, ValueError, SourceError) as error:
        print(f"Guardrail preparation failed: {error}", file=sys.stderr)
        return 1
    print(f"Saved {result['ways']} guardrail ways ({result['length_m']:.0f} m), "
          f"{result['triangles']} triangles in {result['batches']} batches to {result['output']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
