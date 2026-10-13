#!/usr/bin/env python3
"""Build local, rig-preserving zombie LODs without downloading character assets."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import sys
import tempfile

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.zombie_lod import LEVELS, generate_lod


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
    parser.add_argument('--models', type=Path, default=ROOT / 'assets/zombies/models')
    parser.add_argument('--helper', type=Path, default=ROOT / 'build/zombie_mesh_simplify')
    args = parser.parse_args()
    if not args.helper.is_file():
        parser.error('Build the helper first: cmake --build build --target zombie_mesh_simplify -j4')
    paths = sorted(args.models.glob('*.glb'))
    if not paths:
        parser.error('No source GLBs; run tools/download_zombies.py first')
    manifest = {'version': 1, 'models': {}}
    for path in paths:
        source = path.read_bytes()
        fingerprint = hashlib.sha256(source).hexdigest()
        entry = {'source_sha256': fingerprint, 'levels': []}
        for level, (ratio, error) in enumerate(LEVELS, 1):
            data, stats = generate_lod(source, args.helper.resolve(), ratio, error)
            # Content-addressed names keep the old manifest valid until the final
            # atomic replacement, including interrupted or failed generation.
            relative = f'lod{level}/{path.stem}-{hashlib.sha256(data).hexdigest()[:16]}.glb'
            atomic_write(args.models / relative, data)
            entry['levels'].append({'path': relative, 'sha256': hashlib.sha256(data).hexdigest(), **stats})
        manifest['models'][path.name] = entry
        print(path.name + ': ' + ', '.join(f"LOD{i+1} {s['triangles']} tris / {s['vertices']} vertices"
                                         for i, s in enumerate(entry['levels'])), flush=True)
    atomic_write(args.models / 'lods.json', (json.dumps(manifest, indent=2) + '\n').encode())


if __name__ == '__main__':
    main()
