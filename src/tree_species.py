"""Assign stable pine neighborhoods to a completed individual-tree layer."""
from __future__ import annotations

from copy import deepcopy
import hashlib

import numpy as np
from scipy.spatial import cKDTree


PINE_CLUSTER_SEED = 20261019
PINE_CLUSTER_RADIUS_M = 15.0
PINE_CLUSTER_CENTER_PERCENT = 10


def _calibration(asset):
    dimensions = np.asarray([asset['footprint_m'][0], asset['visible_height_m'],
                             asset['footprint_m'][1]], float)
    if dimensions.shape != (3,) or not np.isfinite(dimensions).all() or (dimensions <= 0).any():
        raise ValueError('Tree species conversion requires positive finite asset calibration')
    return dimensions


def assign_pine_clusters(document, pine_asset, *, seed=PINE_CLUSTER_SEED):
    """Use 10% of individual trees as seeds, converting all neighbors within 15 m.

    Random ranking hashes stable IDs, so input order and launches cannot change
    the selection. Distance uses horizontal X/Z trunk positions, not canopy
    bounds or elevation. Converted neighbors never become additional centers.
    Preserve the measured dimensions and each matrix's rotation, reflection and
    shear by rescaling its columns for the new asset's calibrated dimensions.
    ``pine_asset`` uses paths relative to the destination manifest.
    """
    instances = document['instances']
    if not instances:
        return {'pine_cluster_centers': 0, 'pine_cluster_trees': 0, 'new_pines': 0}
    transforms = document['transforms']
    if (not transforms.get('matrix_authoritative') or transforms.get('units') != 'meters'
            or not transforms.get('matrix_layout', '').startswith('column-major')
            or transforms.get('axes') != {'X': 'east', 'Y': 'up', 'Z': 'south'}):
        raise ValueError('Pine clusters require authoritative east/up/south matrices in meters')
    identifiers = [tree['id'] for tree in instances]
    if len(set(identifiers)) != len(identifiers) or not all(isinstance(i, str) and i for i in identifiers):
        raise ValueError('Pine clusters require unique nonempty tree IDs')
    matrices = np.asarray([tree['matrix'] for tree in instances], float)
    if (matrices.shape != (len(instances), 16) or not np.isfinite(matrices).all()
            or not np.allclose(matrices[:, [3, 7, 11, 15]], [0, 0, 0, 1], rtol=0, atol=1e-9)):
        raise ValueError('Invalid tree matrix for pine conversion')
    ranked = sorted(range(len(instances)), key=lambda i: (
        hashlib.sha256(f'pine-clusters:{seed}:{identifiers[i]}'.encode()).digest(), identifiers[i]))
    # Round the percentage to the nearest whole tree; halves round up.
    center_count = (len(instances) * PINE_CLUSTER_CENTER_PERCENT + 50) // 100
    centers = ranked[:center_count]
    positions = matrices[:, [12, 14]]
    if centers:
        distances, _ = cKDTree(positions[centers]).query(positions, k=1)
        selected = np.flatnonzero(distances <= PINE_CLUSTER_RADIUS_M)
    else:
        selected = np.empty(0, dtype=int)
    pine_size = _calibration(pine_asset)
    pine_bounds = pine_asset['geometry_bounds_m']
    lo, hi = np.asarray(pine_bounds['min'], float), np.asarray(pine_bounds['max'], float)
    if lo.shape != (3,) or hi.shape != (3,) or not np.isfinite([lo, hi]).all() or (lo >= hi).any():
        raise ValueError('Pine asset requires valid full geometry bounds')
    replacements = {}
    for index in selected:
        original = instances[index]
        if original['asset'] == 'pine':
            continue
        source_size = _calibration(document['assets'][original['asset']])
        matrix = matrices[index].reshape((4, 4), order='F').copy()
        matrix[:3, :3] *= source_size / pine_size
        tree = deepcopy(original)
        tree['asset'] = 'pine'
        tree['matrix'] = matrix.ravel(order='F').tolist()
        tree['position'] = matrix[:3, 3].tolist()
        tree['scale'] = np.linalg.norm(matrix[:3, :3], axis=0).tolist()
        center = matrix[:3, :3] @ ((lo + hi) / 2) + matrix[:3, 3]
        radius = np.abs(matrix[:3, :3]) @ ((hi - lo) / 2)
        tree['bounds'] = {'min': (center - radius).tolist(), 'max': (center + radius).tolist()}
        replacements[int(index)] = tree
    for index, tree in replacements.items():
        instances[index] = tree
    document['assets']['pine'] = deepcopy(pine_asset)
    document['source']['pine_clusters'] = {
        'version': 1, 'seed': seed, 'radius_m': PINE_CLUSTER_RADIUS_M,
        'center_percent': PINE_CLUSTER_CENTER_PERCENT,
        'center_ids': [identifiers[i] for i in centers], 'instances': len(selected),
        'distance': 'horizontal X/Z trunk centers; no cascading',
        'sizing': 'preserve existing dimensions and authoritative matrix axes',
    }
    # Aggregate geometry bounds must include the new asset's transparent padding.
    if all('bounds' in tree for tree in instances):
        bounds = [tree['bounds'] for tree in instances]
        document['bounds'] = {'min': np.min([b['min'] for b in bounds], axis=0).tolist(),
                              'max': np.max([b['max'] for b in bounds], axis=0).tolist()}
    else:
        document.pop('bounds', None)
    return {'pine_cluster_centers': len(centers), 'pine_cluster_trees': len(selected),
            'new_pines': len(replacements)}
