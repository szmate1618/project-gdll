#!/usr/bin/env python3
"""Prepare scratch zombie LOD candidates and a fingerprinted comparison manifest."""
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
from src.glb import read_glb
from src.zombie_lod import LEVELS, generate_lod

# Keep each family's existing error limit; only its requested triangle ratio changes.
CANDIDATES = (('candidate35', .35, .03), ('candidate25', .25, .03),
              ('candidate15', .15, .08), ('candidate10', .10, .08))


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def atomic_write(path, data):
    path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.NamedTemporaryFile(dir=path.parent, delete=False) as stream:
        temporary = Path(stream.name)
        stream.write(data)
    try:
        os.replace(temporary, path)
    finally:
        temporary.unlink(missing_ok=True)


def geometry_counts(document):
    primitives = [p for mesh in document['meshes'] for p in mesh['primitives']]
    return {'triangles': sum(document['accessors'][p['indices']]['count'] // 3 for p in primitives),
            'vertices': sum(document['accessors'][p['attributes']['POSITION']]['count'] for p in primitives)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--models', type=Path, default=ROOT / 'assets/zombies/models')
    parser.add_argument('--helper', type=Path, default=ROOT / 'build/zombie_mesh_simplify')
    parser.add_argument('--output', type=Path, required=True,
                        help='Scratch directory outside the production model directory')
    args = parser.parse_args()
    models, helper, output = args.models.resolve(), args.helper.resolve(), args.output.resolve()
    if output.is_relative_to(models):
        parser.error('--output must be outside the production model directory')
    if not helper.is_file():
        parser.error('Build the helper first: cmake --build build --target zombie_mesh_simplify -j4')
    paths = sorted(models.glob('*.glb'))
    if not paths:
        parser.error('No source GLBs found; use a prepared local zombie model directory')
    current_bytes = (models / 'lods.json').read_bytes()
    current = json.loads(current_bytes)
    if current.get('version') != 1:
        raise ValueError('Unsupported production LOD manifest version')
    manifest = {'version': 1,
                'description': 'Scratch quality comparison only; production models and manifest are unchanged.',
                'helper_sha256': sha256(helper.read_bytes()),
                'source_manifest_sha256': sha256(current_bytes),
                'candidate_settings': [{'variant': name, 'ratio': ratio, 'error_limit': error}
                                       for name, ratio, error in CANDIDATES],
                'records': [], 'clip_names': {}}
    for source in paths:
        data = source.read_bytes()
        document, binary = read_glb(data)
        source_sha = sha256(data)
        entry = current['models'][source.name]
        if entry['source_sha256'] != source_sha or len(entry['levels']) != len(LEVELS):
            raise ValueError('Stale or incomplete production LOD manifest: ' + source.name)
        original_counts = geometry_counts(document)
        manifest['clip_names'][source.name] = [a.get('name', '') for a in document.get('animations', [])]

        def add_record(variant, path, raw, ratio, error, counts):
            manifest['records'].append({'source_name': source.name, 'variant': variant,
                'path': str(path), 'requested_ratio': ratio, 'error_limit': error,
                'achieved_triangle_ratio': counts['triangles'] / original_counts['triangles'],
                'sha256': sha256(raw), 'source_sha256': source_sha, **counts})

        add_record('original', source, data, 1., None, original_counts)
        for level, ((ratio, error), lod) in enumerate(zip(LEVELS, entry['levels']), 1):
            path = (models / lod['path']).resolve()
            raw = path.read_bytes()
            counts = geometry_counts(read_glb(raw)[0])
            if sha256(raw) != lod['sha256'] or any(counts[k] != lod[k] for k in counts):
                raise ValueError('Production LOD fingerprint/count mismatch: ' + str(path))
            add_record('lod' + str(level), path, raw, ratio, error, counts)
        for name, ratio, error in CANDIDATES:
            raw, counts = generate_lod(data, helper, ratio, error)
            reduced, reduced_binary = read_glb(raw)
            if geometry_counts(reduced) != counts or reduced_binary[:len(binary)] != binary:
                raise ValueError('Candidate geometry or original binary changed: ' + source.name)
            if any(reduced.get(key) != document.get(key)
                   for key in ('nodes', 'scenes', 'scene', 'skins', 'animations')):
                raise ValueError('Candidate rig, scene or animation changed: ' + source.name)
            # Content-addressed files keep an earlier scratch manifest valid on interruption.
            path = output / f'{source.stem}-{name}-{sha256(raw)[:16]}.glb'
            atomic_write(path, raw)
            add_record(name, path, raw, ratio, error, counts)
        print(source.name + ': ' + ', '.join(f"{r['variant']} {r['triangles']} tris"
              for r in manifest['records'] if r['source_name'] == source.name), flush=True)
    if (models / 'lods.json').read_bytes() != current_bytes:
        raise ValueError('Production LOD manifest changed during preparation; rerun with stable inputs')
    target = output / 'manifest.json'
    atomic_write(target, (json.dumps(manifest, indent=2) + '\n').encode())
    print(f'Saved {target}; {len(manifest["records"])} records', flush=True)


if __name__ == '__main__':
    main()
