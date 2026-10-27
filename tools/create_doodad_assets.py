#!/usr/bin/env python3
"""Build three original low-poly ground assets offline; optionally render previews."""
from __future__ import annotations

import argparse
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))

from src.doodad_geometry import models
from src.doodad_assets import build_glb, render_previews


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output-dir", type=Path, default=ROOT / "assets/doodads")
    parser.add_argument("--previews", action="store_true", help="Also render PNGs using the project Python environment")
    args = parser.parse_args()
    # Complete validation before replacing any of the last good assets.
    outputs = {model["id"]: build_glb(model) for model in models()}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    for name, data in outputs.items():
        target = args.output_dir / f"{name}.glb"
        temporary = target.with_suffix(".tmp.glb")
        temporary.write_bytes(data)
        temporary.replace(target)
        print(f"Generated {target} ({len(data):,} bytes)")
    if args.previews:
        render_previews(outputs, args.output_dir / "previews")
        print(f"Rendered previews in {args.output_dir / 'previews'}")


if __name__ == "__main__":
    main()
