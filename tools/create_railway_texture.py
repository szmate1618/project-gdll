#!/usr/bin/env python3
"""Create the deterministic metric railway texture, without downloads."""
import argparse
from pathlib import Path
import sys

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from src.railway_texture import TEXTURE_PATH, create_track_texture


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, default=TEXTURE_PATH)
    args = parser.parse_args()
    args.output.parent.mkdir(parents=True, exist_ok=True)
    create_track_texture().save(args.output)
    print(args.output)


if __name__ == "__main__":
    main()
