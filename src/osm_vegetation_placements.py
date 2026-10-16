"""Publish separate, map-fingerprinted OSM tree and runtime forest layers."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import numpy as np
from pyproj import network
import requests
from shapely import normalize, set_precision
from shapely.geometry import Polygon, box
from shapely.geometry.polygon import orient
from shapely.ops import unary_union

from .buildings import parse_length
from .config import ROOT, load_config
from .data import _download_osm_query
from .osm_vegetation import parse_vegetation, vegetation_query
from .tree_assets import load_impostor
from .tree_map import load_tree_map


def _digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _polygons(geometry):
    if isinstance(geometry, Polygon):
        if geometry.area > .01:
            yield geometry
    else:
        for part in getattr(geometry, "geoms", []):
            yield from _polygons(part)


def forest_patches(forests, terrain_bounds):
    """Union overlapping OSM areas, retain holes and split into <=500 m cells."""
    woodland = set_precision(unary_union([f["geometry"] for f in forests]).intersection(box(*terrain_bounds)), .001)
    patches = []
    if woodland.is_empty:
        return patches
    lo_x, lo_n, hi_x, hi_n = woodland.bounds
    for x in range(math.floor(lo_x / 500), math.floor(hi_x / 500) + 1):
        for n in range(math.floor(lo_n / 500), math.floor(hi_n / 500) + 1):
            pieces = sorted(_polygons(woodland.intersection(box(x*500, n*500, (x+1)*500, (n+1)*500))),
                            key=lambda p: normalize(p).wkb)
            for index, piece in enumerate(pieces):
                piece = orient(piece, sign=1.)
                rings = [piece.exterior, *piece.interiors]
                if len(piece.interiors) > 64 or any(len(r.coords) > 1024 for r in rings):
                    raise ValueError("OSM woodland exceeds runtime ring limits; use smaller forest subdivisions")
                patches.append({"id": f"osm:{x}:{n}:{index}", "region_id": "osm_woodland",
                                "grid_cell": [x, n], "bounds_m": list(piece.bounds), "area_m2": piece.area,
                                "polygon": [list(p) for p in piece.exterior.coords],
                                "holes": [[list(p) for p in r.coords] for r in piece.interiors]})
    return patches


def _instance(feature, terrain, assets):
    tags = feature["tags"]
    species = "pine" if tags.get("leaf_type") == "needleleaved" else "tree"
    asset = assets[species]
    height = parse_length(tags.get("height"))
    tagged_height = height is not None and .5 <= height <= 100
    height = height if tagged_height else asset["visible_height_m"]
    diameter = parse_length(tags.get("diameter_crown"))
    tagged_diameter = diameter is not None and .2 <= diameter <= 100
    scale = np.full(3, height / asset["visible_height_m"])
    if tagged_diameter:
        scale[[0, 2]] = diameter / np.array(asset["footprint_m"])
    x, n = feature["geometry"].coords[0]
    xmin, nmin, xmax, nmax = terrain.bounds
    if not (xmin-.002 <= x <= xmax+.002 and nmin-.002 <= n <= nmax+.002):
        raise ValueError(f"OSM {feature['id']} is outside the published terrain")
    position = [x, float(terrain(x, n)), -n]
    matrix = np.diag([*scale, 1.])
    matrix[:3, 3] = position
    bounds = {key: (np.array(asset["geometry_bounds_m"][key]) * scale + position).tolist()
              for key in ("min", "max")}
    return {"id": "osm:" + feature["id"], "asset": species, "position": position,
            "scale": scale.tolist(), "dimensions_m": [asset["footprint_m"][0]*scale[0], height,
                                                       asset["footprint_m"][1]*scale[2]],
            "matrix": matrix.ravel(order="F").tolist(), "bounds": bounds,
            "source": {"osm_id": feature.get("osm_id", feature["id"]), "tags": tags,
                       "height_source": "OSM height" if tagged_height else "asset default",
                       "crown_source": "OSM diameter_crown" if tagged_diameter else "asset proportions",
                       "placement": feature.get("placement", "mapped trunk coordinate")}}


def tree_instances(trees, rows, terrain, assets):
    """Keep mapped nodes; sample rows at <=9 m and avoid duplicate row trees."""
    features = list(trees)
    occupied = {}

    def remember(point):
        key = (math.floor(point.x/3), math.floor(point.y/3))
        occupied.setdefault(key, []).append(point)

    for tree in trees:
        remember(tree["geometry"])
    for row in rows:
        line = row["geometry"]
        intervals = max(1, math.ceil(line.length / 9))
        for index in range(intervals + 1):
            point = line.interpolate(line.length * index / intervals)
            x, n = math.floor(point.x/3), math.floor(point.y/3)
            if any(point.distance(p) < 3 for i in range(x-1, x+2) for j in range(n-1, n+2)
                   for p in occupied.get((i, j), [])):
                continue
            features.append({**row, "id": row["id"] + f":tree:{index}", "geometry": point,
                             "osm_id": row["id"].split(":")[0],
                             "placement": "approximate position along mapped tree row; <=9 m spacing"})
            remember(point)
    return [_instance(feature, terrain, assets) for feature in features]


def prepare_osm_vegetation(*, config_path=ROOT / "config.json", map_path=ROOT / "output/godollo.glb",
                           input_path=None, output_dir=None, offline=False, refresh=False, root=ROOT):
    # Match the generator's installed projection resources, including offline runs.
    network.set_network_enabled(False)
    config = load_config(Path(config_path))
    config.update(offline=offline, refresh=refresh)
    map_path, root = Path(map_path).resolve(), Path(root).resolve()
    output_dir = Path(output_dir).resolve() if output_dir else map_path.parent / "osm"
    if output_dir in (map_path.parent, root):
        raise ValueError("OSM vegetation must use its own directory to preserve existing vegetation")
    terrain, frame, map_info = load_tree_map(map_path, config)
    if input_path is None:
        directory = root / "data/raw/osm"
        directory.mkdir(parents=True, exist_ok=True)
        with requests.Session() as session:
            session.headers["User-Agent"] = "godollo-osm-vegetation/1.0"
            input_path, provenance = _download_osm_query(config, directory, session,
                vegetation_query(config["bbox"]), "trees, tree rows and woodlands", prefix="vegetation")
    else:
        provenance = {"source": "OpenStreetMap local extract", "license": "ODbL 1.0",
                      "attribution": "© OpenStreetMap contributors"}
    input_path = Path(input_path).resolve()
    source_hash = _digest(input_path)
    payload = json.loads(input_path.read_text(encoding="utf-8"))
    if payload.get("remark") or not isinstance(payload.get("elements"), list):
        raise ValueError("OSM extract is incomplete or has no elements")
    trees, rows, forests = parse_vegetation(payload, frame)
    assets = {kind: load_impostor(root / f"assets/{kind}_impostor.glb") for kind in ("tree", "pine")}
    instances = tree_instances(trees, rows, terrain, assets)
    patches = forest_patches(forests, terrain.bounds)
    source = {**payload.get("source", {}), **provenance, "path": os.path.relpath(input_path, output_dir),
              "sha256": source_hash, "timestamp_osm_base": payload.get("osm3s", {}).get("timestamp_osm_base")}
    tree_document = {"schema": "godollo.tree-instances", "version": 1,
        "map": {**map_info, "path": os.path.relpath(map_path, output_dir)}, "source": source,
        "assets": {kind: {**asset, "path": os.path.relpath(asset["path"], output_dir)}
                   for kind, asset in assets.items()}, "instances": instances,
        "counts": {"instances": len(instances), "mapped_nodes": len(trees), "mapped_rows": len(rows)},
        "transforms": {"units": "meters", "axes": {"X": "east", "Y": "up", "Z": "south"},
                       "matrix_layout": "column-major", "matrix_authoritative": True}}
    forest_document = {"schema": "godollo.forest_patches", "version": 1,
        "description": "OSM natural=wood and landuse=forest areas; clearings retained; overlaps unioned.",
        "coordinate_frame": {"units": "meters", "coordinate_order": ["east", "north"],
            "crs": config["crs"], "origin_projected_m": list(frame["origin"]),
            "origin_wgs84": list(frame["center"]), "bounds_m": list(terrain.bounds)},
        "source": {**source, "map_sha256": map_info["map_sha256"],
                   "map_path": os.path.relpath(map_path, output_dir)},
        "regions": [{"id": "osm_woodland", "sources": [{"id": f["id"], "tags": f["tags"]} for f in forests]}],
        "patches": patches, "subdivision": {"cell_size_m": 500, "coordinate_precision_m": .001},
        "runtime": {"tree_asset": os.path.relpath(root / "assets/tree_impostor.glb", output_dir),
                    "side_texture": os.path.relpath(root / "assets/forests/side.png", output_dir),
                    "top_texture": os.path.relpath(root / "assets/forests/top.png", output_dir)},
        "counts": {"source_areas": len(forests), "patches": len(patches),
                   "total_area_m2": sum(p["area_m2"] for p in patches)}}
    documents = {output_dir / "trees.instances.json": tree_document,
                 output_dir / "forest_patches.json": forest_document}
    protected = {input_path, map_path, Path(config_path).resolve(),
                 *(Path(a["path"]).resolve() for a in assets.values())}
    if any(path in protected for path in documents):
        raise ValueError("OSM output must not overwrite an input, map, config or asset")
    # Stage both outputs before publication. Restore the previous pair on a
    # publication error, so a normal failed preparation retains the last good data.
    staged, originals, published = {}, {}, []
    output_dir.mkdir(parents=True, exist_ok=True)
    try:
        for path, document in documents.items():
            encoded = json.dumps(document, ensure_ascii=False, indent=2, allow_nan=False) + "\n"
            originals[path] = path.read_bytes() if path.exists() else None
            with tempfile.NamedTemporaryFile(dir=output_dir, prefix=".osm-", suffix=".part", delete=False) as stream:
                staged[path] = Path(stream.name)
                stream.write(encoded.encode("utf-8"))
        if _digest(map_path) != map_info["map_sha256"] or _digest(input_path) != source_hash:
            raise ValueError("Map or OSM input changed during vegetation preparation; retry")
        for kind, asset in assets.items():
            if _digest(root / f"assets/{kind}_impostor.glb") != asset["sha256"]:
                raise ValueError("Tree asset changed during vegetation preparation; retry")
        try:
            for path, temporary in staged.items():
                temporary.replace(path)
                published.append(path)
        except OSError:
            for path in reversed(published):
                if originals[path] is None:
                    path.unlink(missing_ok=True)
                else:
                    staged[path].write_bytes(originals[path])
                    staged[path].replace(path)
            raise
    finally:
        for temporary in staged.values():
            temporary.unlink(missing_ok=True)
    return {"output": str(output_dir), **tree_document["counts"], **forest_document["counts"]}
