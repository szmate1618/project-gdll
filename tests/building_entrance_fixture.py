"""Build a small sloping-ground town fixture for walking and render checks."""

from pathlib import Path
import sys

import numpy as np
from PIL import Image
from shapely.geometry import box
import trimesh

from src.buildings import add_buildings
from src.terrain import Terrain


def write_fixture(path, height=1):
    # Front ground is flat at zero; the back establishes a 1 m floor datum.
    north = np.linspace(15, -8, 24)
    heights = np.repeat(np.clip(north[:, None] / 9, 0, 1) * height, 25, axis=1)
    terrain = Terrain(heights, (-6, -8, 18, 15))
    scene = trimesh.Scene()
    scene.add_geometry(terrain.mesh(Image.new('RGB', (4, 4), (100, 125, 85))),
                       geom_name='terrain', node_name='terrain')
    add_buildings(scene, [{'id': 'way/1', 'geometry': box(0, 0, 12, 9),
                           'tags': {'height': '6', 'roof:shape': 'flat'}}], terrain, {})
    rotation = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]])
    scene.apply_transform(rotation)
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    scene.export(str(path))


if __name__ == '__main__':
    write_fixture(sys.argv[1], float(sys.argv[2]) if len(sys.argv) > 2 else 1)
