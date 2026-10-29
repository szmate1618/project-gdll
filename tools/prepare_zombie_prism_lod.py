#!/usr/bin/env python3
"""Build one scratch zombie LOD from ten uncapped triangular columns."""
from __future__ import annotations

import argparse
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.zombie_prism_lod import generate_prism_lod


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--source', type=Path, default=ROOT / 'assets/zombies/models/ZombieMale_C.glb')
    parser.add_argument('--output', type=Path, required=True, help='Scratch GLB path outside production models')
    parser.add_argument('--profile', choices=('standard', 'slim', 'slim2'), default='standard',
                        help='Slim narrows torso/tips; slim2 additionally reshapes the upper torso rim')
    parser.add_argument('--color', choices=('sampled', 'white-vest'), default='white-vest',
                        help='Earlier sampled colors or the recovered off-white torso vest')
    args = parser.parse_args()
    source, output = args.source.resolve(), args.output.resolve()
    if output.is_relative_to(source.parent):
        parser.error('--output must be outside the production model directory')
    original = source.read_bytes()
    model, metadata = generate_prism_lod(original, color_mode=args.color.replace('-', '_'),
                                        profile=args.profile)
    if source.read_bytes() != original:
        raise RuntimeError('Source changed while preparing prototype')
    metadata['source'] = str(source)
    metadata['output'] = str(output)
    atomic_write(output, model)
    atomic_write(output.with_suffix('.json'), (json.dumps(metadata, indent=2) + '\n').encode())
    print(f'Saved {output}: {args.profile}, {args.color}; '
          '10 open prisms, 60 vertices, 60 triangles, 60 boundary edges; no caps')


if __name__ == '__main__':
    main()
