#!/usr/bin/env python3
"""Build three rig-preserving LoDs for the approved dog appearances, offline."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import sys

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT))
from src.zombie_lod import generate_lod
from tools.prepare_zombie_lods import atomic_write

# Dogs already start at 496 triangles. Keep the quadruped rig and all clips;
# humanoid prism LoDs cannot represent this skeleton.
LEVELS = ((.65, .04), (.4, .10), (.2, .20))


def prepare(models: Path, helper: Path):
    manifest = {'version': 1, 'models': {}}
    for variant in range(1, 4):
        path = models / f'undead_dog_variant_{variant}.glb'
        source = path.read_bytes()
        levels = []
        for level, (ratio, error) in enumerate(LEVELS, 1):
            data, stats = generate_lod(source, helper.resolve(), ratio, error)
            digest = hashlib.sha256(data).hexdigest()
            relative = f'lod{level}/{path.stem}-{digest[:16]}.glb'
            atomic_write(models / relative, data)
            levels.append({'path': relative, 'sha256': digest, **stats})
        manifest['models'][path.name] = {'source_sha256': hashlib.sha256(source).hexdigest(), 'levels': levels}
        print(f'{path.name}: ' + ', '.join(f"LOD{i+1} {item['triangles']} triangles" for i, item in enumerate(levels)), flush=True)
    for name, entry in manifest['models'].items():
        if hashlib.sha256((models / name).read_bytes()).hexdigest() != entry['source_sha256']:
            raise ValueError(f'Dog source changed during preparation: {name}')
        for level in entry['levels']:
            if hashlib.sha256((models / level['path']).read_bytes()).hexdigest() != level['sha256']:
                raise ValueError(f'Dog LoD changed during preparation: {level["path"]}')
    atomic_write(models / 'lods.json', (json.dumps(manifest, indent=2) + '\n').encode())


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, default=ROOT / 'assets/undead_dog/models')
    parser.add_argument('--helper', type=Path, default=ROOT / 'build/zombie_mesh_simplify')
    args = parser.parse_args()
    prepare(args.models, args.helper)


if __name__ == '__main__':
    main()
