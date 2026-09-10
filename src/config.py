"""Configuration and the shared projected coordinate frame."""
from __future__ import annotations

import json
import math
from pathlib import Path

import numpy as np
from pyproj import CRS, Transformer
from shapely.geometry import Polygon

ROOT = Path(__file__).resolve().parents[1]


def load_config(path: Path) -> dict:
    with path.open(encoding="utf-8") as stream:
        config = json.load(stream)
    bbox = config.get("bbox", [])
    if len(bbox) != 4 or not all(isinstance(v, (int, float)) and math.isfinite(v) for v in bbox):
        raise ValueError("bbox must contain four finite numbers: [west, south, east, north].")
    w, s, e, n = bbox
    if not (-180 <= w < e <= 180 and -85 <= s < n <= 85):
        raise ValueError("bbox must be an ordered longitude/latitude rectangle, away from the poles.")
    crs = CRS.from_user_input(config.get("crs", "EPSG:32634"))
    if not crs.is_projected or any(abs(a.unit_conversion_factor - 1) > 1e-9 for a in crs.axis_info):
        raise ValueError("crs must be a projected coordinate system with meter units.")
    config["crs"] = crs.to_string()
    checks = [
        ("terrain", "spacing_m", 15, 2, 100),
        ("terrain", "smoothing_sigma", .6, 0, 5),
        ("terrain", "texture_size", 512, 32, 4096),
        ("buildings", "floor_height_m", 3, 1, 10),
        ("buildings", "default_height_m", 8, 1, 200),
        ("roads", "lane_width_m", 3, 1, 10),
        ("roads", "offset_m", .25, .01, 5),
        ("roads", "sample_spacing_m", 8, 1, 100),
        ("network", "timeout_seconds", 120, 1, 600),
        ("network", "retries", 3, 1, 5),
        ("sentinel", "max_cloud_cover", 20, 0, 100),
    ]
    for section, key, default, low, high in checks:
        value = config.setdefault(section, {}).setdefault(key, default)
        if not isinstance(value, (float, int)) or not math.isfinite(value) or not low <= value <= high:
            raise ValueError(f"{section}.{key} must be between {low} and {high}.")
    for section, key in [("terrain", "texture_size"), ("network", "retries")]:
        if config[section][key] != int(config[section][key]):
            raise ValueError(f"{section}.{key} must be an integer.")
        config[section][key] = int(config[section][key])
    config["sentinel"].setdefault("datetime", "2025-05-01T00:00:00Z/2025-09-30T23:59:59Z")
    return config


def coordinate_frame(config: dict) -> dict:
    w, s, e, n = config["bbox"]
    transformer = Transformer.from_crs("EPSG:4326", config["crs"], always_xy=True)
    center = ((w + e) / 2, (s + n) / 2)
    origin = transformer.transform(*center)
    # Densify all four edges before projecting, retaining the true geographic clip.
    lon = np.r_[np.linspace(w, e, 21), np.full(21, e), np.linspace(e, w, 21), np.full(21, w)]
    lat = np.r_[np.full(21, s), np.linspace(s, n, 21), np.full(21, n), np.linspace(n, s, 21)]
    x, y = transformer.transform(lon, lat)
    polygon = Polygon(np.column_stack((np.asarray(x) - origin[0], np.asarray(y) - origin[1])))
    bounds = polygon.bounds
    width, height = bounds[2] - bounds[0], bounds[3] - bounds[1]
    if max(width, height) > 10000:
        raise ValueError("Prototype bbox is limited to 10 km per side; choose a smaller region.")
    if max(abs(v) for v in origin) > 1e8 or not polygon.is_valid:
        raise ValueError("Cannot project this bbox. Choose a suitable local metric CRS.")
    if math.ceil(width / config["terrain"]["spacing_m"]) * math.ceil(height / config["terrain"]["spacing_m"]) > 2000000:
        raise ValueError("Terrain would exceed two million grid cells; increase terrain.spacing_m.")
    return {"transformer": transformer, "origin": origin, "center": center,
            "clip": polygon, "bounds": bounds, "crs": config["crs"]}
