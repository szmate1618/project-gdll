"""Fictional station interior, clipped to one recorded OSM building and its courts.

The source definition is in plan metres: X along the street facade and Y into
the building. Exterior photographs guide opening proportions; room locations
are invented for gameplay. This module never changes another building's rules.
"""
from functools import lru_cache
import json
from pathlib import Path

import numpy as np
import trimesh
from trimesh.visual.material import PBRMaterial
from shapely import constrained_delaunay_triangles
from shapely.geometry import LineString, Point, Polygon, box

from .authored_building_layout import _approaches, _partitions, _rooms
from .building_details import _facades, _rectangle
from .config import coordinate_frame
from .wall_openings import cut_wall_openings

LAYOUT_PATH = Path(__file__).resolve().parents[1] / 'assets/buildings/godollo-police-station/layout.json'


@lru_cache(maxsize=1)
def definition():
    return json.loads(LAYOUT_PATH.read_text(encoding='utf-8'))


def reference_feature(config):
    data = definition()
    frame = coordinate_frame(config)

    def project(ring):
        return np.array([frame['transformer'].transform(*p) for p in ring]) - frame['origin']

    polygon = Polygon(project(data['reference_footprint_lonlat']),
                      [project(h) for h in data['reference_courtyards_lonlat']])
    return {'id': data['osm_id'], 'geometry': polygon,
            'tags': {'building': 'public', 'building:levels': '4', 'amenity': 'police'}}


def entrance_canopy_polygon(config):
    frame = coordinate_frame(config)
    points = np.array([frame['transformer'].transform(*p)
                       for p in definition()['entrance_canopy']['reference_footprint_lonlat']])
    return Polygon(points - frame['origin'])


def station_canopy(config, floor, elevation, frame):
    """OSM's glass entrance roof is an open canopy, never a solid building."""
    data = definition()['entrance_canopy']
    polygon = entrance_canopy_polygon(config)
    bottom = floor + data['clearance_m']
    roof = trimesh.creation.extrude_polygon(polygon, data['thickness_m'])
    roof.apply_translation([0, 0, bottom])
    roof.visual = trimesh.visual.TextureVisuals(material=PBRMaterial(
        name='station_glass_canopy', baseColorFactor=[151, 179, 187, 220],
        metallicFactor=0, roughnessFactor=.25, alphaMode='BLEND', doubleSided=True))
    # Two slim supports at the street corners leave the ramp and entry open.
    origin, u, v, _ = frame
    local = (np.array(polygon.exterior.coords) - origin) @ np.array([u, v]).T
    corners = sorted(local[:-1], key=lambda p: p[1])[:2]
    posts = []
    for p in corners:
        xy = origin + u * p[0] + v * p[1]
        ground = float(elevation(*xy)) - .02
        post = trimesh.creation.box([data['post_width_m'], data['post_width_m'], bottom - ground])
        post.apply_translation([*xy, (ground + bottom) / 2])
        posts.append(post)
    supports = trimesh.util.concatenate(posts)
    supports.visual = trimesh.visual.TextureVisuals(material=PBRMaterial(
        name='station_canopy_frame', baseColorFactor=[76, 89, 96, 255], roughnessFactor=.8))
    return {'canopy': roof, 'canopy_supports': supports}


def station_frame(feature, polygon, config):
    data = definition()
    if feature['id'] != data['osm_id']:
        return None
    reference = reference_feature(config)['geometry']
    if not polygon.is_valid or polygon.symmetric_difference(reference).area > .02:
        raise ValueError('Station override requires its complete recorded footprint and five courtyards')
    frame = coordinate_frame(config)
    anchors = np.array([frame['transformer'].transform(*p) for p in data['front_reference_lonlat']])
    anchors -= frame['origin']
    origin = anchors[0]
    u = anchors[1] - origin
    u /= np.linalg.norm(u)
    v = np.array([-u[1], u[0]])

    def local(ring):
        return (np.array(ring.coords) - origin) @ np.array([u, v]).T

    return origin, u, v, Polygon(local(polygon.exterior), [local(h) for h in polygon.interiors])


def station_tags(tags):
    return {**tags, 'height': str(definition()['ceiling_m']), 'roof:shape': 'flat'}


def _merge_remainders(rooms):
    """Absorb thin court/stair remnants into their adjoining wide corridor.

    A concave strip can survive an erosion only at one rounded corner, without
    enough room for a player's centre. It must not become a separate room with
    an unreachable door at the midpoint of its long boundary.
    """
    for room in list(rooms):
        region = Polygon(room['polygon'])
        if region.buffer(-.4).area >= .1:
            continue
        candidates = []
        for other in rooms:
            if other is room or other['kind'] not in {'corridor', 'lobby', 'stair_area'}:
                continue
            adjacent = Polygon(other['polygon'])
            merged = adjacent.union(region)
            if merged.geom_type == 'Polygon' and not merged.interiors:
                shared = adjacent.boundary.intersection(region.boundary).length
                if shared > 1:
                    candidates.append((shared, other, merged))
        if not candidates:
            raise ValueError('Station has an unusable isolated room remnant')
        _, other, merged = max(candidates, key=lambda item: item[0])
        other['polygon'] = np.array(merged.exterior.coords)[:-1].tolist()
        rooms.remove(room)
    return rooms


def prepare_layout(frame, floor):
    origin, u, v, outline = frame
    data = definition()
    rooms, partitions, stairs = [], [], []
    for storey, level in enumerate(data['floor_heights_m']):
        requested = [*data['stair_areas'], data['lobby'], *data['corridors']]
        if storey == 0:
            requested.extend(data['ground_rooms'])
        labels = data['departments'][storey]
        for j, (y0, y1) in enumerate(zip(data['room_grid_y'], data['room_grid_y'][1:])):
            for i, (x0, x1) in enumerate(zip(data['room_grid_x'], data['room_grid_x'][1:])):
                label = labels[(j * (len(data['room_grid_x']) - 1) + i) % len(labels)]
                requested.append([f'{label} {storey}.{j+1}{i+1}', 'living', x0, y0, x1, y1])
        fitted = _merge_remainders(_rooms(outline, requested))
        for room in fitted:
            room['storey'] = storey
        partitions.extend(_partitions(fitted, storey, outline))
        # Two alternating ramp bays leave a full landing at each end. Flights
        # on consecutive levels never sit above the same floor cutout.
        if storey + 1 < len(data['floor_heights_m']):
            for core in data['stairs']:
                room = len(rooms) + next(i for i, r in enumerate(fitted) if r['label'] == core['label'])
                ramp = core['bays'][storey % 2]
                x0, y0, x1, y1 = ramp
                if not outline.covers(box(x0 - .15, y0 - .7, x1 + .15, y1 + .7)):
                    raise ValueError('Station stair or landing leaves the mapped shell')
                stairs.append({'room': room, 'ramp': ramp, 'opening': ramp, 'axis': 1,
                               'ascending': storey % 2 == 0, 'bottom': level + (.01 if storey == 0 else 0),
                               'top': data['floor_heights_m'][storey + 1]})
        rooms.extend(fitted)
    # Runtime's simple-polygon triangulator receives a disjoint, exact tiling.
    # Courtyard holes must never be bridged by slabs, including the top ceiling.
    regions = [np.array(t.exterior.coords)[:-1].tolist()
               for t in constrained_delaunay_triangles(outline).geoms]
    return {'version': 2, 'origin': [*origin.tolist(), floor], 'u': [*u.tolist(), 0],
            'v': [*v.tolist(), 0], 'ceiling': data['ceiling_m'] - .02,
            'upper_floors': data['floor_heights_m'][1:],
            'footprint': np.array(outline.exterior.coords)[:-1].tolist(),
            'courtyards': [np.array(h.coords)[:-1].tolist() for h in outline.interiors],
            'floor_regions': regions, 'door_height': data['door_height_m'], 'finish': 'institutional',
            'rooms': rooms, 'partitions': partitions, 'stairs': stairs}


def station_facade_details(polygon, wall, roof, elevation, frame):
    """Replace generic openings and fit windows between authored partitions."""
    origin, u, v, outline = frame
    data = definition()
    floor = float(np.float32(roof.vertices[:, 2].min() - data['ceiling_m']))
    layout = prepare_layout(frame, floor)
    facades = _facades(polygon)
    openings, occupied, approaches = {}, {}, []

    def install(index, center, width, bottom, height, kind):
        facade = facades[index]
        if center - width / 2 < .18 or center + width / 2 > facade.length - .18:
            return False
        span = box(center - width / 2 - .12, bottom, center + width / 2 + .12, bottom + height)
        if any(span.intersects(previous) for previous in occupied.get(index, ())):
            return False
        occupied.setdefault(index, []).append(span)
        openings.setdefault(index, []).append((kind, _rectangle(facade, center, width, bottom, height)))
        return True

    for label, x, y, width in data['ground_exterior_doors']:
        point = origin + u * x + v * y
        index = min(range(len(facades)), key=lambda i: facades[i].line.distance(Point(point)))
        facade = facades[index]
        along = float(np.dot(point - facade.start, facade.direction))
        if facade.line.distance(Point(point)) > .15 or not install(index, along, width, floor, data['door_height_m'], 'doors'):
            raise ValueError(f'Cannot fit station exterior door: {label}')
        approaches.append((facade.start + facade.direction * along, facade.direction, facade.normal, floor, width))

    windows = data['windows']
    for storey, level in enumerate(data['floor_heights_m']):
        walls = []
        for part in layout['partitions']:
            if part['storey'] != storey:
                continue
            ends = [[part['position'], part['low']], [part['position'], part['high']]]
            if part['axis'] == 1:
                ends = [p[::-1] for p in ends]
            walls.append(LineString(ends))
        for index, facade in enumerate(facades):
            count = int(facade.length // windows['spacing_m'])
            for center in np.linspace(0, facade.length, count + 2)[1:-1]:
                for shift in (0, -.3, .3, -.6, .6):
                    along = center + shift
                    a = facade.start + facade.direction * (along - windows['width_m'] / 2)
                    b = facade.start + facade.direction * (along + windows['width_m'] / 2)
                    pane = LineString((np.array([a, b]) - origin) @ np.array([u, v]).T)
                    if any(pane.distance(part) < .205 for part in walls):
                        continue
                    if install(index, along, windows['width_m'], floor + level + windows['sill_m'],
                               windows['height_m'], 'windows'):
                        break
    detailed, doors, panes, crawl = cut_wall_openings(wall, facades, openings)
    wall.metadata['building_floor'] = [*origin.tolist(), floor]
    wall.metadata['authored_floorplan'] = layout
    return (detailed, doors, panes, _approaches(approaches, elevation), crawl, None, None,
            {'doors': len(approaches),
             'windows': sum(kind == 'windows' for entries in openings.values() for kind, _ in entries)})
