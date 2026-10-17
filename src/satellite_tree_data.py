"""Native Sentinel raster input, placement serialization and atomic publication."""
from __future__ import annotations

import json
import os
from pathlib import Path
import tempfile

import numpy as np
from pyproj import CRS, Transformer
import rasterio
from rasterio.features import geometry_window, rasterize
from shapely.affinity import translate
from shapely.geometry import box, mapping
from shapely.ops import transform as transform_geometry

from .forest_comparison import file_sha256


def _project_geometry(geometry, frame, target_crs):
    absolute = translate(geometry, *frame["origin"])
    if CRS.from_user_input(frame["crs"]) != CRS.from_user_input(target_crs):
        converter = Transformer.from_crs(frame["crs"], target_crs, always_xy=True)
        absolute = transform_geometry(converter.transform, absolute)
    return absolute


def read_satellite(path, frame, terrain_bounds, forest_geometry, settings, *, training_geometry=None):
    """Retain the source pixel grid and affine; rasterize training and exclusion separately."""
    with rasterio.open(path) as source:
        if source.crs is None or source.count < 3 or source.dtypes[:3] != ("uint8",)*3:
            raise ValueError("Satellite detection requires a georeferenced uint8 RGB raster")
        map_geometry = frame["clip"].intersection(box(*terrain_bounds))
        footprint = _project_geometry(map_geometry, frame, source.crs)
        window = geometry_window(source, [mapping(footprint)])
        if window.width * window.height > 10_000_000:
            raise ValueError("Satellite map window exceeds ten million native pixels")
        transform = source.window_transform(window)
        rgb = np.moveaxis(source.read([1, 2, 3], window=window), 0, -1)
        valid = np.all(source.read_masks([1, 2, 3], window=window) > 0, axis=0)
        valid &= np.any(rgb != 0, axis=-1)

        def mask(geometry, all_touched=False):
            if geometry.is_empty:
                return np.zeros(rgb.shape[:2], dtype=bool)
            geometry = _project_geometry(geometry, frame, source.crs)
            return rasterize([(mapping(geometry), 1)], out_shape=rgb.shape[:2], transform=transform,
                             all_touched=all_touched, fill=0, dtype="uint8").astype(bool)

        valid &= mask(map_geometry)
        reference_geometry = forest_geometry if training_geometry is None else training_geometry
        forest_centres = mask(reference_geometry) & valid
        excluded = mask(forest_geometry, all_touched=True)
        training = mask(reference_geometry.buffer(-settings.training_inset_m)) & valid
        converter = Transformer.from_crs(source.crs, frame["crs"], always_xy=True)

        def local_coordinates(rows, columns):
            columns, rows = np.asarray(columns)+.5, np.asarray(rows)+.5
            x = transform.a*columns + transform.b*rows + transform.c
            y = transform.d*columns + transform.e*rows + transform.f
            east, north = converter.transform(x, y, errcheck=True)
            return np.asarray(east)-frame["origin"][0], np.asarray(north)-frame["origin"][1]

        rows = np.array([rgb.shape[0]/2, rgb.shape[0]/2, rgb.shape[0]/2+1])
        columns = np.array([rgb.shape[1]/2, rgb.shape[1]/2+1, rgb.shape[1]/2])
        east, north = local_coordinates(rows, columns)
        pixel_size = [float(np.hypot(east[i]-east[0], north[i]-north[0])) for i in (1, 2)]
        metadata = {"path": str(Path(path).resolve()), "crs": source.crs.to_string(),
                    "dimensions_pixels": [source.width, source.height],
                    "pixel_edge_affine": list(source.transform)[:6],
                    "window": {"column": int(window.col_off), "row": int(window.row_off),
                               "width": int(window.width), "height": int(window.height)},
                    "window_pixel_edge_affine": list(transform)[:6],
                    "pixel_axis_lengths_in_map_m": pixel_size}
    return rgb, valid, forest_centres, excluded, training, local_coordinates, metadata


def default_satellite_image(root, forest_path, document):
    candidates = [(forest_path, document)]
    reference = root / "forest_patches_astra.json"
    if reference != forest_path and reference.is_file():
        candidates.append((reference, json.loads(reference.read_text(encoding="utf-8"))))
    for config_path, config in candidates:
        satellite = config.get("source", {}).get("satellite", {})
        if satellite.get("path"):
            path = (config_path.parent / satellite["path"]).resolve()
            if not path.is_file() or file_sha256(path) != satellite["sha256"]:
                raise ValueError("Recorded native satellite image is missing or stale; use --image to select one")
            return path
    raise ValueError("No native Sentinel image recorded; supply --image with a local georeferenced RGB TIFF")


def make_instance(candidate, rgb, asset, terrain, pixel_offset):
    east, north = candidate["east_m"], candidate["north_m"]
    position = [east, float(terrain(east, north)), -north]
    if not np.isfinite(position).all():
        raise ValueError("Candidate has no finite published terrain support")
    matrix = np.eye(4)
    matrix[:3, 3] = position
    column, row = candidate["column"]+pixel_offset[0], candidate["row"]+pixel_offset[1]
    return {"id": f"satellite:pixel:{column}:{row}", "asset": "tree", "position": position,
            "scale": [1., 1., 1.], "dimensions_m": [asset["footprint_m"][0], asset["visible_height_m"],
                                                       asset["footprint_m"][1]],
            "matrix": matrix.ravel(order="F").tolist(),
            "bounds": {key: (np.asarray(asset["geometry_bounds_m"][key])+position).tolist()
                       for key in ("min", "max")},
            "source": {"method": candidate.get("method", "forest_colour_similarity_outside_forest"), "pixel_column": column,
                       "pixel_row": row, "score": candidate["score"],
                       "rgb": rgb[candidate["row"], candidate["column"]].tolist(),
                       **({"negative_example_keep_score":candidate["negative_example_keep_score"]}
                          if "negative_example_keep_score" in candidate else {}),
                       "position_method": "native satellite pixel centre; not a resolved trunk",
                       "dimensions_source": "calibrated asset default; not measured from satellite"}}


def publish_detections(output_dir, documents, review_image, fingerprints):
    """Prepare all artifacts first and retain the previous set on publication failure."""
    output_dir.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(prefix=".satellite-trees-", dir=output_dir) as scratch:
        scratch = Path(scratch)
        for name, document in documents.items():
            encoded = json.dumps(document, indent=2, ensure_ascii=False, allow_nan=False)+"\n"
            (scratch/name).write_text(encoded, encoding="utf-8")
            json.loads((scratch/name).read_text(encoding="utf-8"))
        review_image.save(scratch / "review.png")
        for path, expected in fingerprints.items():
            if file_sha256(path) != expected:
                raise ValueError(f"Detection input changed during processing: {path}")
        names = [*documents, "review.png"]
        originals, published = {}, []
        try:
            for name in names:
                target = output_dir/name
                originals[name] = target.read_bytes() if target.exists() else None
                os.replace(scratch/name, target)
                published.append(name)
        except OSError:
            for name in reversed(published):
                if originals[name] is None:
                    (output_dir/name).unlink(missing_ok=True)
                else:
                    (scratch/name).write_bytes(originals[name])
                    os.replace(scratch/name, output_dir/name)
            raise


def validate_forest_frame(document, frame, terrain, map_info, role):
    recorded = document["coordinate_frame"]
    if (document["source"]["map_sha256"] != map_info["map_sha256"]
            or CRS.from_user_input(recorded["crs"]) != CRS.from_user_input(frame["crs"])
            or not np.allclose(recorded["origin_projected_m"], frame["origin"], rtol=0, atol=1e-6)
            or not np.allclose(recorded["bounds_m"], terrain.bounds, rtol=0, atol=.002)):
        raise ValueError(f"Forest {role} data does not match the published map/frame")
