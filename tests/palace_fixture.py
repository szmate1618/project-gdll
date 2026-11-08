"""Small offline palace + flat ground, using the recorded OSM reference outline."""
from pathlib import Path
import sys

import numpy as np
from PIL import Image
from shapely.geometry import Polygon
import trimesh

from src.buildings import add_buildings
from src.config import load_config, coordinate_frame
from src.palace_floorplan import definition
from src.terrain import Terrain


def feature(config):
    data = definition()
    frame = coordinate_frame(config)
    points = np.array([frame["transformer"].transform(*p) for p in data["reference_footprint_lonlat"]])
    points -= frame["origin"]
    return {"id": data["osm_id"], "geometry": Polygon(points), "tags": {"building": "yes", "building:levels": "2"}}


def write_fixture(path):
    config = load_config(Path(__file__).resolve().parents[1] / "config.json")
    palace = feature(config)
    x0, y0, x1, y1 = palace["geometry"].bounds
    terrain = Terrain(np.zeros((24, 24)), (x0 - 20, y0 - 20, x1 + 20, y1 + 20))
    scene = trimesh.Scene()
    scene.add_geometry(terrain.mesh(Image.new("RGB", (4, 4), (100, 125, 85))), node_name="terrain", geom_name="terrain")
    stats = add_buildings(scene, [palace], terrain, config)
    if stats["buildings"] != 1:
        raise ValueError("Palace fixture generation failed")
    scene.apply_transform(np.array([[1., 0., 0., 0.], [0., 0., 1., 0.], [0., -1., 0., 0.], [0., 0., 0., 1.]]))
    Path(path).parent.mkdir(parents=True, exist_ok=True)
    scene.export(str(path))


if __name__ == "__main__":
    write_fixture(sys.argv[1])
