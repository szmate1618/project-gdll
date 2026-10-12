"""Query mapped poles and publish columns grounded on the existing GLB terrain."""
from __future__ import annotations

from collections import Counter
import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import requests

from .buildings import parse_length
from .config import ROOT, load_config
from .data import _download_osm_query
from .hev_pole_placements import make_hev_columns, map_railways, HEV_POLE_SPACING_M
from .tree_map import load_tree_map

# Approximation defaults in meters: height and square cross-section width.
POLE_DIMENSIONS = {
    "street_lamp": (7.0, .20),
    "power_pole": (9.0, .30),
    "utility_pole": (8.0, .25),
    "power_tower": (25.0, 1.0),
}


def pole_query(bbox):
    west, south, east, north = bbox
    bounds = f"{south},{west},{north},{east}"
    return ('[out:json][timeout:90];\n(\n'
            f'  node["highway"="street_lamp"]({bounds});\n'
            f'  node["power"~"^(pole|tower)$"]({bounds});\n'
            f'  node["man_made"="utility_pole"]({bounds});\n'
            ');\nout body;\n')


def pole_kind(tags):
    # One physical column for a power pole that also carries a streetlight.
    if tags.get("power") == "tower":
        return "power_tower"
    if tags.get("power") == "pole":
        return "power_pole"
    if tags.get("man_made") == "utility_pole":
        return "utility_pole"
    if tags.get("highway") == "street_lamp":
        return "street_lamp"
    return None


def make_columns(payload, config, terrain, frame):
    """Convert WGS84 nodes once to X east / Y up / Z south, in meters."""
    columns, seen = [], set()
    west, south, east, north = config["bbox"]
    for node in payload["elements"]:
        if node.get("type") != "node":
            continue
        tags = node.get("tags", {})
        kind = pole_kind(tags)
        if kind is None:
            continue
        node_id = node["id"]
        if isinstance(node_id, bool) or not isinstance(node_id, int) or node_id <= 0:
            raise ValueError("Pole node requires a positive integer OSM ID")
        if node_id in seen:
            continue
        seen.add(node_id)
        lon, lat = node["lon"], node["lat"]
        if (not all(isinstance(v, (float, int)) and not isinstance(v, bool)
                    and math.isfinite(v) for v in (lon, lat))):
            raise ValueError(f"Pole node/{node_id} has invalid coordinates")
        if not (west <= lon <= east and south <= lat <= north):
            continue
        x, northing = frame["transformer"].transform(lon, lat)
        x, northing = x - frame["origin"][0], northing - frame["origin"][1]
        y = float(terrain(x, northing))
        default_height, width = POLE_DIMENSIONS[kind]
        height = parse_length(tags.get("height"))
        if height is None or not .5 <= height <= 100:
            height = default_height
        if not all(math.isfinite(v) for v in (x, y, northing)):
            raise ValueError(f"Pole node/{node_id} cannot be placed on map terrain")
        columns.append({"id": f"node/{node_id}", "kind": kind,
                        "position": [x, y, -northing], "height_m": height,
                        "width_m": width, "tags": tags})
    return sorted(columns, key=lambda column: column["id"])


def prepare_poles(*, config_path=ROOT / "config.json", map_path=ROOT / "output/godollo.glb",
                  output=None, offline=False, refresh=False, root=ROOT):
    config = load_config(Path(config_path))
    config.update(offline=offline, refresh=refresh)
    map_path = Path(map_path)
    output = Path(output) if output is not None else map_path.with_name("poles.instances.json")
    if output.resolve() == map_path.resolve():
        raise ValueError("Pole output must not overwrite the map")
    terrain, frame, metadata = load_tree_map(map_path, config)
    directory = Path(root) / "data/raw/osm"
    directory.mkdir(parents=True, exist_ok=True)
    with requests.Session() as session:
        session.headers["User-Agent"] = "godollo-pole-layer/1.0 (OpenStreetMap scene generator)"
        path, provenance = _download_osm_query(config, directory, session, pole_query(config["bbox"]),
                                              "streetlights and utility poles", prefix="poles")
    payload = json.loads(path.read_text(encoding="utf-8"))
    columns = make_columns(payload, config, terrain, frame)
    railways, railway_source = map_railways(map_path, frame, root)
    columns.extend(make_hev_columns(railways, terrain))
    counts = dict(Counter(column["kind"] for column in columns))
    document = {"schema": "godollo.pole-instances", "version": 2,
                "map": metadata, "source": provenance, "columns": columns,
                "hev": {"source": railway_source, "spacing_m": HEV_POLE_SPACING_M,
                        "placement": "approximate paired main-track midpoint"},
                "counts": {"columns": len(columns), **counts}}
    # Validate serialization before publishing; preserve the last good layer on failure.
    encoded = json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False) + "\n"
    with map_path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != metadata["map_sha256"]:
            raise ValueError("Map changed during pole preparation; retry with the completed map")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", dir=output.parent,
                                         prefix=output.name + ".", suffix=".tmp", delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(encoded)
            stream.flush()
            os.fsync(stream.fileno())
        json.loads(temporary.read_text(encoding="utf-8"))
        temporary.replace(output)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()
    return {"output": str(output), **document["counts"]}
