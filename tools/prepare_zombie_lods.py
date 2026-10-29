#!/usr/bin/env python3
"""Prepare local LOD1/2 meshes and Slim 1 prism LOD3 without downloading assets."""
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
from src.zombie_prism_lod import generate_prism_lod, production_prism_color_mode


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def reusable_near_levels(models, entry, fingerprint):
    """Retain the published LOD1/2 bytes when their source and hashes still match."""
    if entry.get('source_sha256') != fingerprint or len(entry.get('levels', [])) != 3:
        return []
    root = models.resolve()
    for level in entry['levels'][:2]:
        relative = Path(level['path'])
        path = (root / relative).resolve()
        if relative.is_absolute() or not path.is_relative_to(root) or not path.is_file() or \
                hashlib.sha256(path.read_bytes()).hexdigest() != level['sha256']:
            return []
    return [dict(level) for level in entry['levels'][:2]]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, default=ROOT / 'assets/zombies/models')
    parser.add_argument('--helper', type=Path, default=ROOT / 'build/zombie_mesh_simplify')
    args = parser.parse_args()
    paths = sorted(args.models.glob('*.glb'))
    if not paths:
        parser.error('No source GLBs; run tools/download_zombies.py first')
    manifest_path = args.models / 'lods.json'
    previous = json.loads(manifest_path.read_text()) if manifest_path.is_file() else {}
    manifest = {'version': 1, 'models': {}}
    reused = 0
    for path in paths:
        source = path.read_bytes()
        fingerprint = hashlib.sha256(source).hexdigest()
        levels = reusable_near_levels(args.models, previous.get('models', {}).get(path.name, {}), fingerprint)
        reused += len(levels)
        if not levels:
            if not args.helper.is_file():
                parser.error('Build the LOD1/2 helper first: cmake --build build --target zombie_mesh_simplify -j4')
            for level, (ratio, error) in enumerate(LEVELS[:2], 1):
                data, stats = generate_lod(source, args.helper.resolve(), ratio, error)
                digest = hashlib.sha256(data).hexdigest()
                relative = f'lod{level}/{path.stem}-{digest[:16]}.glb'
                atomic_write(args.models / relative, data)
                levels.append({'path': relative, 'sha256': digest, **stats})
        color_mode = production_prism_color_mode(path.name)
        data, metadata = generate_prism_lod(source, profile='slim', color_mode=color_mode)
        digest = hashlib.sha256(data).hexdigest()
        relative = f'lod3/{path.stem}-slim1-{digest[:16]}.glb'
        atomic_write(args.models / relative, data)
        atomic_write((args.models / relative).with_suffix('.json'),
                     (json.dumps(metadata, indent=2) + '\n').encode())
        levels.append({'path': relative, 'sha256': digest, 'triangles': metadata['topology']['triangles'],
                       'vertices': metadata['topology']['vertices'], 'method': 'prism-slim1', 'color_mode': color_mode})
        manifest['models'][path.name] = {'source_sha256': fingerprint, 'levels': levels}
        print(path.name + ': ' + ', '.join(f"LOD{i+1} {level['triangles']} tris / {level['vertices']} vertices"
                                         for i, level in enumerate(levels)), flush=True)
    # Preserve the old active set until every source and generated file is verified.
    for name, entry in manifest['models'].items():
        if hashlib.sha256((args.models / name).read_bytes()).hexdigest() != entry['source_sha256']:
            raise RuntimeError('Zombie source changed during preparation: ' + name)
        for level in entry['levels']:
            if hashlib.sha256((args.models / level['path']).read_bytes()).hexdigest() != level['sha256']:
                raise RuntimeError('Zombie LOD changed during preparation: ' + level['path'])
    atomic_write(manifest_path, (json.dumps(manifest, indent=2) + '\n').encode())
    print(f'Activated Slim 1 LOD3; reused {reused} existing LOD1/2 meshes', flush=True)


if __name__ == '__main__':
    main()
