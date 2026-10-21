#!/usr/bin/env python3
"""Sample reproducible, physically free endpoints on the original replay map."""
import argparse
import hashlib
import json
from pathlib import Path
import random

import numpy as np
import shapely


class PhysicalGeometry:
    """Original padded OBBs and trunk circles; no navigation or field geometry."""
    def __init__(self, document, radius=.3, skin=.002, building_extra=.05):
        self.radius, self.skin, self.building_extra = radius, skin, building_extra
        records = np.asarray(document['obstacles'], dtype=np.float64).reshape((-1, 7))
        if not np.isfinite(records).all() or np.any(records[:, 5:] < 0):
            raise ValueError('Nonfinite or negative obstacle geometry')
        self.kinds = records[:, 0].astype(np.int64)
        if np.any((self.kinds < 0) | (self.kinds > 2)) or np.any(records[:, 0] != self.kinds):
            raise ValueError('Unknown obstacle kind')
        self.centers = records[:, 1:3]
        lengths = np.linalg.norm(records[:, 3:5], axis=1)
        if np.any(lengths <= 0):
            raise ValueError('Obstacle axis must be nonzero')
        self.axes = records[:, 3:5] / lengths[:, None]
        self.perpendiculars = np.stack((-self.axes[:, 1], self.axes[:, 0]), axis=1)
        padding = radius + skin + (self.kinds == 0) * building_extra
        self.halves = records[:, 5:7] + padding[:, None]
        self.radii = records[:, 5:7].max(axis=1) + radius + skin
        extent = (np.abs(self.axes) * self.halves[:, :1] +
                  np.abs(self.perpendiculars) * self.halves[:, 1:])
        extent[self.kinds == 1] = self.radii[self.kinds == 1, None]
        self.tree = shapely.STRtree(shapely.box(self.centers[:, 0]-extent[:, 0],
            self.centers[:, 1]-extent[:, 1], self.centers[:, 0]+extent[:, 0],
            self.centers[:, 1]+extent[:, 1]))
        low = np.asarray(document['raster']['minimum'], dtype=np.float64) + radius + skin
        high = np.asarray(document['raster']['maximum'], dtype=np.float64) - radius - skin
        if not np.isfinite(np.r_[low, high]).all() or np.any(low >= high):
            raise ValueError('Invalid map bounds')
        self.minimum, self.maximum = low, high

    def clear(self, point):
        point = np.asarray(point, dtype=np.float64)
        if not np.isfinite(point).all() or np.any(point <= self.minimum) or np.any(point >= self.maximum):
            return False
        indices = self.tree.query(shapely.Point(*point))
        if not len(indices):
            return True
        offsets = point-self.centers[indices]
        circles = self.kinds[indices] == 1
        if np.any(np.einsum('ij,ij->i', offsets[circles], offsets[circles]) <= self.radii[indices[circles]]**2):
            return False
        rectangles = indices[~circles]
        offsets = offsets[~circles]
        x = np.einsum('ij,ij->i', offsets, self.axes[rectangles])
        z = np.einsum('ij,ij->i', offsets, self.perpendiculars[rectangles])
        return not np.any((np.abs(x) <= self.halves[rectangles, 0]) &
                          (np.abs(z) <= self.halves[rectangles, 1]))


def prepare(scene, output, population=1000, seed=1515146562):
    if population < 1 or population > 100000:
        raise ValueError('Population must be 1..100000')
    if output.exists():
        raise ValueError('Output already exists; preserve it and choose a new filename')
    raw = scene.read_bytes()
    document = json.loads(raw)
    geometry = PhysicalGeometry(document)
    random_source = random.Random(seed)
    attempts = 0

    def endpoint():
        nonlocal attempts
        for _ in range(100000):
            attempts += 1
            point = [float(np.float32(random_source.uniform(a, b)))
                     for a, b in zip(geometry.minimum, geometry.maximum)]
            if geometry.clear(point):
                return point
        raise RuntimeError('Endpoint rejection exceeded its candidate limit')

    requests = [{'id': index+1, 'start': endpoint(), 'goal': endpoint()}
                for index in range(population)]
    result = {'seed': seed, 'source': str(scene.resolve()),
              'source_sha256': hashlib.sha256(raw).hexdigest(),
              'radius': geometry.radius, 'skin': geometry.skin,
              'building_extra': geometry.building_extra, 'attempts': attempts,
              'endpoint_policy': 'Independent uniform map samples rounded to float32, rejecting original padded physical geometry only; no field-circle, component or reachability rejection.',
              'prng': 'Python random.Random MT19937', 'requests': requests}
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_name(output.name+'.tmp')
    temporary.write_text(json.dumps(result, indent=2)+'\n')
    temporary.replace(output)
    print(f'{population} requests; {attempts} candidates; {attempts-2*population} rejected; {output}')
    return result


if __name__ == '__main__':
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--scene', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--population', type=int, default=1000)
    parser.add_argument('--seed', type=int, default=1515146562)
    arguments = parser.parse_args()
    prepare(arguments.scene, arguments.output, arguments.population, arguments.seed)
