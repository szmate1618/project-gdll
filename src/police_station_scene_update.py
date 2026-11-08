"""Replace only the station in a published map, preserving all other geometry."""
from io import BytesIO
import numpy as np
from shapely.geometry import MultiPoint, Point
import trimesh

from .authored_scene_update import update_authored_building
from .buildings import add_buildings
from .glb import read_glb, pack_glb
from .police_station_floorplan import definition, reference_feature, entrance_canopy_polygon
from .scene_mesh_update import replace_scene_meshes
from .tree_map import _accessor


def prepare_station_patch(original, terrain, config):
    data = definition()
    feature = reference_feature(config)
    name = 'building_' + data['osm_id'].replace('/', '_') + '_0'
    document, original_binary = read_glb(original)
    wall = next((m for m in document['meshes'] if m.get('name') == name + '_walls'), None)
    if wall is None:
        raise ValueError('Selected map does not contain the mapped Gödöllő police station')
    bounds = feature['geometry'].bounds
    positions = document['accessors'][wall['primitives'][0]['attributes']['POSITION']]
    if not (np.allclose(positions['min'][:2], bounds[:2], atol=.02, rtol=0) and
            np.allclose(positions['max'][:2], bounds[2:], atol=.02, rtol=0)):
        raise ValueError('Existing station footprint differs from the recorded outline')
    points = _accessor(BytesIO(original_binary), document, (0, len(original_binary)),
                       wall['primitives'][0]['attributes']['POSITION'], positions=True)[:, :2]
    footprint = feature['geometry']
    cloud = MultiPoint(points)
    if (any(footprint.boundary.distance(Point(p)) > .002 for p in points) or
            any(cloud.distance(Point(p)) > .002 for ring in [footprint.exterior, *footprint.interiors]
                for p in ring.coords)):
        raise ValueError('Existing station perimeter or courtyard rings differ from the recorded outline')
    scene = trimesh.Scene()
    stats = add_buildings(scene, [feature], terrain, config)
    if stats['buildings'] != 1:
        raise ValueError('Station geometry preparation failed')
    canopy_name = 'building_' + data['entrance_canopy']['osm_id'].replace('/', '_') + '_0'
    old_canopy = next((m for m in document['meshes'] if m.get('name') == canopy_name + '_walls'), None)
    if old_canopy is not None:
        bounds = entrance_canopy_polygon(config).bounds
        positions = document['accessors'][old_canopy['primitives'][0]['attributes']['POSITION']]
        if not (np.allclose(positions['min'][:2], bounds[:2], atol=.02, rtol=0) and
                np.allclose(positions['max'][:2], bounds[2:], atol=.02, rtol=0)):
            raise ValueError('Existing entrance canopy no longer matches its recorded outline')
    updated, binary = replace_scene_meshes(original, scene, (name + '_', canopy_name + '_'))
    metadata = updated['scenes'][updated.get('scene', 0)].setdefault('extras', {})
    prepared = scene.geometry[name + '_walls'].metadata['authored_floorplan']
    summary = {'version': 2, 'fictional_interior': True, 'source': data['sources'],
               'storeys': len(data['floor_heights_m']), 'courtyards': len(feature['geometry'].interiors),
               'rooms': len(prepared['rooms']), 'stairs': len(prepared['stairs']),
               'open_entrance_canopy': data['entrance_canopy']['osm_id'],
               'exterior_doors': stats['doors'], 'windows': stats['windows']}
    metadata['police_station_interior'] = summary
    updated['buffers'][0]['byteLength'] = len(binary)
    return pack_glb(updated, binary), summary


def update_station(map_path, config, companions=()):
    return update_authored_building(map_path, config, companions, prepare_station_patch, 'police_station')
