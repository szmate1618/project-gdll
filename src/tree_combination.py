"""Combine mapped OSM trees/rows with image proposals, preserving mapped trunks."""
from __future__ import annotations

from copy import deepcopy
import json
import math
import os
from pathlib import Path
import tempfile

import numpy as np
from scipy.spatial import cKDTree

from .forest_comparison import file_sha256


def combine_trees(osm_path, tuned2_path, map_path, output_path, *, duplicate_distance_m=6.):
    """Keep every OSM instance; suppress tuned2 centres within 6 m of an OSM tree.

    Positions come from authoritative column-major matrices in local metres.
    Asset transforms and dimensions stay intact. All inputs are fingerprinted,
    runtime references are rebased, and the result is published atomically.
    """
    osm_path, tuned2_path, map_path, output_path = [Path(p).resolve() for p in
                                                 (osm_path, tuned2_path, map_path, output_path)]
    if not math.isfinite(duplicate_distance_m) or duplicate_distance_m < 0:
        raise ValueError('Duplicate distance must be finite nonnegative metres')
    hashes = {p: file_sha256(p) for p in (osm_path, tuned2_path, map_path)}
    documents = [json.loads(p.read_text()) for p in (osm_path, tuned2_path)]
    result = deepcopy(documents[1])
    result['assets'], result['instances'] = {}, []
    positions, seen_ids = [], set()
    for path, document in zip((osm_path, tuned2_path), documents):
        if document.get('schema') != 'godollo.tree-instances' or document.get('version') != 1:
            raise ValueError('Unsupported tree layer for combination')
        if (document['map']['map_sha256'] != hashes[map_path]
                or (path.parent/document['map']['path']).resolve() != map_path):
            raise ValueError('Tree layers must match the published map')
        for key in ('crs', 'origin_projected_m', 'units'):
            if document['map'][key] != result['map'][key]:
                raise ValueError('Tree layers use different coordinate frames')
        transforms = document['transforms']
        if (not transforms.get('matrix_authoritative') or transforms.get('units') != 'meters'
                or not transforms.get('matrix_layout', '').startswith('column-major')
                or transforms.get('axes') != {'X': 'east', 'Y': 'up', 'Z': 'south'}):
            raise ValueError('Tree layers require authoritative east/up/south matrices')
        for name, original in document['assets'].items():
            record = deepcopy(original)
            asset = (path.parent/record['path']).resolve()
            if file_sha256(asset) != record['sha256']:
                raise ValueError(f'Stale tree asset: {asset}')
            hashes[asset] = record['sha256']
            record['path'] = os.path.relpath(asset, output_path.parent)
            if name in result['assets'] and result['assets'][name] != record:
                raise ValueError(f'Conflicting tree asset: {name}')
            result['assets'][name] = record
        points = []
        for instance in document['instances']:
            matrix = np.asarray(instance['matrix'], float)
            if (matrix.shape != (16,) or not np.isfinite(matrix).all()
                    or not np.allclose(matrix[[3, 7, 11, 15]], [0, 0, 0, 1], rtol=0, atol=1e-9)):
                raise ValueError('Invalid tree instance matrix')
            if instance['asset'] not in document['assets'] or instance['id'] in seen_ids:
                raise ValueError('Unknown tree asset or duplicate instance ID')
            seen_ids.add(instance['id'])
            points.append([matrix[12], matrix[14]])
        positions.append(np.asarray(points).reshape(-1, 2))
    if output_path in hashes:
        raise ValueError('Combined trees must not overwrite an input')
    keep = np.ones(len(positions[1]), dtype=bool)
    if len(positions[0]) and len(positions[1]):
        distances, _ = cKDTree(positions[0]).query(positions[1], k=1)
        keep = distances > duplicate_distance_m
    result['instances'] = documents[0]['instances'] + [p for p, retain in zip(documents[1]['instances'], keep) if retain]
    result['map']['path'] = os.path.relpath(map_path, output_path.parent)
    result['counts'] = {'instances': len(result['instances']), 'osm_instances': len(positions[0]),
                        'tuned2_instances': int(keep.sum()), 'tuned2_suppressed_near_osm': int((~keep).sum())}
    result.pop('bounds', None)
    result['source'] = {'method': 'osm_trees_and_rows_with_tuned2', 'default_preparation': 'osm-tuned2',
                        'duplicate_distance_m': duplicate_distance_m,
                        'layers': [{'path': os.path.relpath(path, output_path.parent), 'sha256': hashes[path],
                                    'source': document['source']} for path, document in
                                   zip((osm_path, tuned2_path), documents)]}
    output_path.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='.combined-trees-', dir=output_path.parent) as directory:
        temporary = Path(directory)/output_path.name
        temporary.write_text(json.dumps(result, indent=2, allow_nan=False)+'\n')
        for path, expected in hashes.items():
            if file_sha256(path) != expected:
                raise ValueError(f'Tree combination input changed: {path}')
        os.replace(temporary, output_path)
    return {'output': str(output_path), **result['counts']}
