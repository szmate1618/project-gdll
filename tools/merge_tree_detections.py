#!/usr/bin/env python3
"""Combine tree detector CSV exports while retaining coordinates and provenance."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


ROOT = Path(__file__).resolve().parents[1]
if str(ROOT) not in sys.path:
    sys.path.insert(0, str(ROOT))

from src.tree_csv import merge_detections


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("inputs", nargs="+", help="CSV paths, quoted glob patterns, or directories (searched recursively)")
    parser.add_argument("-o", "--output", type=Path, default=ROOT / "data/processed/tree_detections.csv",
                        help="Merged CSV destination (default: data/processed/tree_detections.csv in this project)")
    args = parser.parse_args(argv)
    try:
        result = merge_detections(args.inputs, args.output)
    except (ValueError, OSError) as error:
        print(f"Merge failed: {error}", file=sys.stderr)
        return 1
    print(f"Merged {result['rows']} detections from {len(result['inputs'])} CSV files into {result['output']}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
