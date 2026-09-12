"""Georeference detector boxes and prepare reusable tree instances, offline."""
from __future__ import annotations

import hashlib
import json
import math
import os
from pathlib import Path
import tempfile

import numpy as np
from pyproj import CRS, Transformer, network
from pyproj.exceptions import ProjError
import rasterio

from .config import ROOT, load_config
from .tree_assets import load_impostor
from .tree_csv import iter_detections
from .tree_map import load_tree_map


def _digest(path):
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def _identifier(*parts):
    return hashlib.sha256(json.dumps(parts, ensure_ascii=False).encode()).hexdigest()[:24]


class RasterCatalog:
    """Read only raster headers, resolving each CSV's own image independently."""

    def __init__(self, image_roots, source_crs, target_crs):
        self.roots = [Path(p).expanduser().resolve() for p in image_roots]
        for root in self.roots:
            if not root.exists():
                raise ValueError(f"Image root does not exist: {root}")
        self.override = CRS.from_user_input(source_crs) if source_crs else None
        if self.override and not (self.override.is_projected or self.override.is_geographic):
            raise ValueError("--source-crs must identify a projected or geographic CRS.")
        self.target_crs = target_crs
        self.resolved, self.rasters = {}, {}
        self._by_name = None

    def _resolve(self, row):
        key = (row["source_csv"], row["image_path"])
        if key in self.resolved:
            return self.resolved[key]
        original = Path(row["image_path"]).expanduser()
        candidate = original if original.is_absolute() else Path(row["source_csv"]).parent / original
        if candidate.is_file():
            path = candidate.resolve()
        else:
            if self._by_name is None:
                self._by_name = {}
                for root in self.roots:
                    paths = [root] if root.is_file() else root.rglob("*")
                    for entry in paths:
                        if entry.suffix.lower() in (".tif", ".tiff") and entry.is_file():
                            self._by_name.setdefault(entry.name, set()).add(entry.resolve())
            # Also handle image paths copied from a Windows detector machine.
            name = str(original).replace("\\", "/").rsplit("/", 1)[-1]
            matches = sorted(self._by_name.get(name, []))
            if not matches:
                raise ValueError(f"Missing source TIFF {row['image_path']!r}; supply its directory with --image-root.")
            if len(matches) != 1:
                raise ValueError(f"Ambiguous source TIFF {name!r}: {matches}; use an exact image_path in the CSV.")
            path = matches[0]
        self.resolved[key] = path
        return path

    def get(self, row):
        path = self._resolve(row)
        if path in self.rasters:
            return self.rasters[path]
        with rasterio.open(path) as dataset:
            stored_crs = CRS.from_user_input(dataset.crs) if dataset.crs else None
            crs = self.override or stored_crs
            if crs is None or not (crs.is_projected or crs.is_geographic):
                raise ValueError(f"{path}: missing or local-only CRS ({stored_crs}); specify --source-crs, "
                                 "for example EPSG:23700 for confirmed Hungarian EOV data.")
            transform = dataset.transform
            if (not np.isfinite(tuple(transform)).all()
                    or abs(transform.a * transform.e - transform.b * transform.d) < 1e-20):
                raise ValueError(f"{path}: raster has no usable affine pixel transform.")
            stat = path.stat()
            info = {
                "id": _identifier(str(path)), "path": str(path), "crs": crs.to_string(),
                "stored_crs": stored_crs.to_string() if stored_crs else None,
                "crs_overridden": self.override is not None,
                "pixel_to_world": list(transform)[:6],
                "image_size_pixels": [dataset.width, dataset.height],
                "size_bytes": stat.st_size, "mtime_ns": stat.st_mtime_ns,
            }
        result = {"info": info, "transform": transform,
                  "to_map": Transformer.from_crs(crs, self.target_crs, always_xy=True),
                  "to_wgs84": Transformer.from_crs(crs, "EPSG:4326", always_xy=True)}
        self.rasters[path] = result
        return result


def _detection(row, raster):
    """Keep supplied world centers; use pixel boxes only for horizontal extent."""
    bbox = np.array([float(row[k]) for k in ("xmin", "ymin", "xmax", "ymax")])
    center = np.array([float(row[k]) for k in ("world_x", "world_y")])
    pixel = (bbox[:2] + bbox[2:]) / 2
    provided = [bool(row.get(k, "").strip()) for k in ("pixel_x", "pixel_y")]
    if any(provided):
        if not all(provided):
            raise ValueError("pixel_x and pixel_y must be supplied together.")
        pixel = np.array([float(row[k]) for k in ("pixel_x", "pixel_y")])
        if np.any(pixel < bbox[:2] - .5) or np.any(pixel > bbox[2:] + .5):
            raise ValueError("pixel center lies outside its detection box.")
    inverse = ~raster["transform"]
    calculated_pixel = np.array([inverse.a * center[0] + inverse.b * center[1] + inverse.c,
                                 inverse.d * center[0] + inverse.e * center[1] + inverse.f])
    # Accept both common pixel-center conventions, separated by half a pixel.
    # This is a consistency check only: it never shifts supplied world centers.
    if not np.allclose(calculated_pixel, pixel, rtol=0, atol=.75):
        raise ValueError("world_x/world_y disagree with this TIFF's pixel coordinates; "
                         "check image_path, full-image pixel offsets, and source CRS.")
    size = raster["info"]["image_size_pixels"]
    if np.any(pixel < -.5) or np.any(pixel > np.asarray(size) + .5):
        raise ValueError("detection center falls outside its source TIFF.")
    score = float(row["score"]) if row.get("score", "").strip() else None
    if score is not None and not 0 <= score <= 1:
        raise ValueError("score must be between 0 and 1.")
    return bbox, center, score


def make_instance(row, raster, terrain, frame, asset, species, height_m, bbox, center, score):
    """Fit asset X/Z to the two projected pixel-box axes; leave the trunk upright.

    The matrix is authoritative, including raster rotation, reflection and shear.
    scale reports axis lengths; it is not a complete transform by itself.
    """
    transform = raster["transform"]
    width, depth = bbox[2:] - bbox[:2]
    column = np.array([transform.a, transform.d]) * width
    line = np.array([transform.b, transform.e]) * depth
    sample = np.array([center, center - column / 2, center + column / 2,
                       center - line / 2, center + line / 2])
    east, north = raster["to_map"].transform(sample[:, 0], sample[:, 1], errcheck=True)
    projected = np.column_stack((east, north))
    if not np.isfinite(projected).all():
        raise ValueError("detection cannot be projected into the map CRS.")
    local_east, local_north = projected[0] - frame["origin"]
    # The float32 exported envelope can differ by fractions of a millimeter.
    xmin, ymin, xmax, ymax = terrain.bounds
    if not (xmin - .001 <= local_east <= xmax + .001 and ymin - .001 <= local_north <= ymax + .001):
        raise ValueError("detection is inside the configured area but outside the published terrain.")
    axis_x, axis_z = projected[2] - projected[1], projected[4] - projected[3]
    dimensions = [float(np.linalg.norm(axis_x)), height_m, float(np.linalg.norm(axis_z))]
    if min(dimensions) <= 0:
        raise ValueError("detection has a collapsed projected footprint.")
    base_width, base_depth = asset["footprint_m"]
    matrix = np.eye(4)
    matrix[:3, 0] = [axis_x[0] / base_width, 0, -axis_x[1] / base_width]
    matrix[:3, 1] = [0, height_m / asset["visible_height_m"], 0]
    matrix[:3, 2] = [axis_z[0] / base_depth, 0, -axis_z[1] / base_depth]
    matrix[:3, 3] = [local_east, terrain(local_east, local_north), -local_north]
    if abs(np.linalg.det(matrix[:3, :3])) < 1e-12:
        raise ValueError("detection has a singular projected footprint.")
    bounds = asset["geometry_bounds_m"]
    lo, hi = np.array(bounds["min"]), np.array(bounds["max"])
    bounds_center = matrix[:3, :3] @ ((lo + hi) / 2) + matrix[:3, 3]
    bounds_radius = np.abs(matrix[:3, :3]) @ ((hi - lo) / 2)
    return {
        "id": _identifier(row["source_csv"], row["source_row"]), "asset": species,
        "position": matrix[:3, 3].tolist(),
        "scale": [dimensions[0] / base_width, height_m / asset["visible_height_m"], dimensions[2] / base_depth],
        "dimensions_m": dimensions, "matrix": matrix.T.ravel().tolist(),
        "bounds": {"min": (bounds_center - bounds_radius).tolist(), "max": (bounds_center + bounds_radius).tolist()},
        "source": {"csv": _identifier(row["source_csv"]), "row": int(row["source_row"]),
                   "raster": raster["info"]["id"], "world_xy": center.tolist(),
                   "bbox_pixels": bbox.tolist(), "label": row.get("label", ""), "score": score},
    }


def prepare_instances(input_path=ROOT / "data/processed/tree_detections.csv",
                      output_path=ROOT / "output/trees.instances.json", *,
                      config_path=ROOT / "config.json", map_path=ROOT / "output/godollo.glb",
                      image_roots=(), source_crs=None, species="tree", height_m=None, min_score=0):
    """Atomically write a versioned JSON instance set without modifying the map."""
    if species not in ("tree", "pine"):
        raise ValueError("species must be tree or pine.")
    if not math.isfinite(min_score) or not 0 <= min_score <= 1:
        raise ValueError("min_score must be between 0 and 1.")
    input_path, output_path, config_path, map_path = [Path(p).expanduser().resolve()
                                                    for p in (input_path, output_path, config_path, map_path)]
    asset_paths = [ROOT / "assets" / f"{kind}_impostor.glb" for kind in ("tree", "pine")]
    asset_path = ROOT / "assets" / f"{species}_impostor.glb"
    if output_path in (input_path, config_path, map_path, *asset_paths):
        raise ValueError("Output must be separate from the CSV, config, map and tree asset.")
    # Use installed projection resources only, even if the shell enabled grids
    # from PROJ's network service. This preprocessing workflow is fully local.
    network.set_network_enabled(False)
    config = load_config(config_path)
    terrain, frame, map_info = load_tree_map(map_path, config)
    asset = load_impostor(asset_path)
    explicit_height = height_m is not None
    height_m = asset["visible_height_m"] if height_m is None else float(height_m)
    if not math.isfinite(height_m) or height_m <= 0:
        raise ValueError("height_m must be finite and greater than zero.")
    catalog = RasterCatalog(image_roots, source_crs, config["crs"])
    counts = {"input": 0, "outside_area": 0, "below_min_score": 0, "instances": 0}
    sources, identifiers = {}, set()
    bounds_min, bounds_max = np.full(3, np.inf), np.full(3, -np.inf)
    initial_stat = input_path.stat()
    output_path.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(mode="w", encoding="utf-8", prefix=f".{output_path.name}.",
                                         suffix=".part", dir=output_path.parent, delete=False) as stream:
            temporary = Path(stream.name)
            stream.write('{"schema":"godollo.tree-instances","version":1,"instances":[\n')
            for row in iter_detections(input_path):
                counts["input"] += 1
                sources[_identifier(row["source_csv"])] = row["source_csv"]
                try:
                    if output_path == Path(row["source_csv"]):
                        raise ValueError("Output must not overwrite an original source CSV.")
                    raster = catalog.get(row)
                    if output_path == Path(raster["info"]["path"]):
                        raise ValueError("Output must not overwrite a source TIFF.")
                    bbox, center, score = _detection(row, raster)
                    if min_score > 0 and score is None:
                        raise ValueError("--min-score requires a score on every detection.")
                    if score is not None and score < min_score:
                        counts["below_min_score"] += 1
                        continue
                    lon, lat = raster["to_wgs84"].transform(*center, errcheck=True)
                    w, s, e, n = config["bbox"]
                    if not (w <= lon <= e and s <= lat <= n):
                        counts["outside_area"] += 1
                        continue
                    instance = make_instance(row, raster, terrain, frame, asset, species, height_m, bbox, center, score)
                    if instance["id"] in identifiers:
                        raise ValueError("repeated source_csv/source_row provenance; merge each original CSV only once.")
                    identifiers.add(instance["id"])
                    if counts["instances"]:
                        stream.write(',\n')
                    stream.write(json.dumps(instance, separators=(",", ":"), ensure_ascii=False, allow_nan=False))
                    counts["instances"] += 1
                    bounds_min = np.minimum(bounds_min, instance["bounds"]["min"])
                    bounds_max = np.maximum(bounds_max, instance["bounds"]["max"])
                except (ValueError, OSError, ProjError) as error:
                    raise ValueError(f"{row['source_csv']}:{row['source_row']}: {error}") from error
            input_hash = _digest(input_path)
            current_stat = input_path.stat()
            if (current_stat.st_size, current_stat.st_mtime_ns) != (initial_stat.st_size, initial_stat.st_mtime_ns):
                raise ValueError("Input CSV changed during processing; retry once the detector/merge has finished.")
            metadata = {
                "map": {**map_info, "path": os.path.relpath(map_path, output_path.parent)},
                "assets": {species: {**asset, "path": os.path.relpath(asset_path, output_path.parent)}},
                "input": {"path": str(input_path), "sha256": input_hash},
                "source_csvs": sources,
                "rasters": {r["info"]["id"]: r["info"] for r in catalog.rasters.values()},
                "settings": {"source_crs_override": catalog.override.to_string() if catalog.override else None,
                             "species": species, "height_m": height_m, "min_score": min_score},
                "transforms": {"units": "meters", "axes": {"X": "east", "Y": "up", "Z": "south"},
                               "matrix_layout": "column-major; multiply matrix * asset vertex",
                               "matrix_authoritative": True, "footprint": "image column and row axes",
                               "height_source": "explicit height_m" if explicit_height else "asset height",
                               "placement": "CSV world center on published terrain; upright trunk base"},
                "counts": counts,
                "bounds": {"min": bounds_min.tolist(), "max": bounds_max.tolist()} if counts["instances"] else None,
            }
            stream.write('\n],')
            stream.write(json.dumps(metadata, ensure_ascii=False, allow_nan=False, indent=2)[1:])
            stream.write('\n')
            stream.flush()
            os.fsync(stream.fileno())
        temporary.replace(output_path)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return {"output": output_path, **counts, "rasters": len(catalog.rasters)}
