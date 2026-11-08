"""Small offline station fixture with the recorded five-courtyard footprint."""
from pathlib import Path
import sys
import numpy as np
from PIL import Image
import trimesh

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
from src.buildings import add_buildings
from src.config import load_config
from src.police_station_floorplan import definition, reference_feature, entrance_canopy_polygon
from src.terrain import Terrain


def write_fixture(path, legacy_canopy=False):
    config = load_config(Path(__file__).resolve().parents[1] / 'config.json')
    feature = reference_feature(config)
    x0, y0, x1, y1 = feature['geometry'].bounds
    terrain = Terrain(np.zeros((24, 24)), (x0 - 25, y0 - 25, x1 + 25, y1 + 25))
    scene = trimesh.Scene()
    scene.add_geometry(terrain.mesh(Image.new('RGB', (4, 4), (105, 125, 95))), node_name='terrain', geom_name='terrain')
    canopy = {'id': definition()['entrance_canopy']['osm_id'],
              'geometry': entrance_canopy_polygon(config), 'tags': {'building': 'roof'}}
    stats = add_buildings(scene, [feature, canopy], terrain, config)
    if legacy_canopy:
        # Reproduce the old map's solid extrusion of the separate OSM roof way.
        add_buildings(scene, [canopy], terrain, config)
    if stats['buildings'] != 1:
        raise ValueError('Station fixture generation failed')
    scene.apply_transform(np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]]))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    scene.export(str(path))
    return stats


if __name__ == '__main__':
    print(write_fixture(sys.argv[1]))
