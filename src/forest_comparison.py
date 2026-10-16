"""Compare forest coverage in a shared local east/north metre frame.

Areas are measured from polygon unions (with holes), never summed metadata.
The imagery is only a backdrop; it does not determine the comparison metrics.
"""
from __future__ import annotations

import hashlib
import json
from pathlib import Path

import numpy as np
from pyproj import CRS
from shapely.geometry import Polygon, box
from shapely.ops import unary_union


def load_forest(path: Path) -> tuple[dict, object]:
    """Read and validate a forest configuration without changing its bytes."""
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    if document.get("schema") != "godollo.forest_patches" or document.get("version") != 1:
        raise ValueError(f"Unsupported forest schema: {path}")
    frame = document["coordinate_frame"]
    origin = np.asarray(frame["origin_projected_m"], dtype=float)
    bounds = np.asarray(frame["bounds_m"], dtype=float)
    crs = CRS.from_user_input(frame["crs"])
    if (frame["units"] != "meters" or frame["coordinate_order"] != ["east", "north"]
            or origin.shape != (2,) or bounds.shape != (4,)
            or not np.isfinite(origin).all() or not np.isfinite(bounds).all()
            or bounds[0] >= bounds[2] or bounds[1] >= bounds[3] or not crs.is_projected
            or any(abs(axis.unit_conversion_factor - 1) > 1e-9 for axis in crs.axis_info)):
        raise ValueError("Forest frame must contain finite local east/north metres")
    polygons, identifiers = [], set()
    for patch in document["patches"]:
        if not patch["id"] or patch["id"] in identifiers:
            raise ValueError("Forest patch IDs must be nonempty and unique")
        identifiers.add(patch["id"])
        for ring in [patch["polygon"], *patch["holes"]]:
            points = np.asarray(ring, dtype=float)
            if (points.ndim != 2 or points.shape[1] != 2 or len(points) < 4
                    or not np.isfinite(points).all() or not np.array_equal(points[0], points[-1])):
                raise ValueError("Forest rings must be closed finite [east, north] coordinates")
        polygon = Polygon(patch["polygon"], patch["holes"])
        if polygon.is_empty or not polygon.is_valid or polygon.area <= 0:
            raise ValueError(f"Invalid forest polygon: {patch['id']}")
        if not polygon.exterior.is_ccw or any(ring.is_ccw for ring in polygon.interiors):
            raise ValueError(f"Incorrect forest ring winding: {patch['id']}")
        if not box(*bounds).buffer(.002).covers(polygon):
            raise ValueError(f"Forest patch outside recorded map bounds: {patch['id']}")
        x0, y0, x1, y1 = polygon.bounds
        if x1 - x0 > 500.001 or y1 - y0 > 500.001:
            raise ValueError(f"Forest patch exceeds 500 m grid: {patch['id']}")
        polygons.append(polygon)
    coverage = unary_union(polygons)
    if sum(p.area for p in polygons) - coverage.area > .1:
        raise ValueError("Forest patches overlap within a dataset")
    return document, coverage


def compare_forests(old: dict, old_geometry, new: dict, new_geometry) -> tuple[dict, dict]:
    """Return true area changes; reject unrelated maps or coordinate frames."""
    a, b = old["coordinate_frame"], new["coordinate_frame"]
    if (CRS.from_user_input(a["crs"]) != CRS.from_user_input(b["crs"])
            or a["units"] != b["units"] or a["coordinate_order"] != b["coordinate_order"]
            or not np.allclose(a["origin_projected_m"], b["origin_projected_m"], rtol=0, atol=1e-6)
            or not np.allclose(a["bounds_m"], b["bounds_m"], rtol=0, atol=.002)):
        raise ValueError("Cannot compare forest data in different coordinate frames")
    digest = old["source"].get("map_sha256")
    if not digest or digest != new["source"].get("map_sha256"):
        raise ValueError("Cannot compare forest data with different map fingerprints")
    layers = {"shared": old_geometry.intersection(new_geometry),
              "added": new_geometry.difference(old_geometry),
              "removed": old_geometry.difference(new_geometry)}
    combined_area = old_geometry.union(new_geometry).area
    metrics = {"old_area_m2": old_geometry.area, "new_area_m2": new_geometry.area,
               **{f"{name}_area_m2": geometry.area for name, geometry in layers.items()},
               "net_change_m2": new_geometry.area - old_geometry.area,
               "intersection_over_union": layers["shared"].area / combined_area if combined_area else 1.,
               "old_patch_count": len(old["patches"]), "new_patch_count": len(new["patches"])}
    return metrics, layers


def satellite_backdrop(path: Path, frame: dict, max_pixels: int = 1600):
    """Warp a georeferenced satellite RGB raster onto the local map bounds."""
    import rasterio
    from rasterio.enums import Resampling
    from rasterio.warp import reproject
    from rasterio.transform import from_bounds
    from PIL import Image

    x0, y0, x1, y1 = frame["bounds_m"]
    east, north = frame["origin_projected_m"]
    scale = max_pixels / max(x1 - x0, y1 - y0)
    width, height = max(1, round((x1 - x0) * scale)), max(1, round((y1 - y0) * scale))
    transform = from_bounds(x0 + east, y0 + north, x1 + east, y1 + north, width, height)
    pixels = np.zeros((3, height, width), dtype=np.uint8)
    with rasterio.open(path) as source:
        if source.count < 3 or source.crs is None or source.dtypes[:3] != ("uint8",) * 3:
            raise ValueError("Satellite backdrop requires georeferenced uint8 RGB imagery")
        for band in range(3):
            reproject(rasterio.band(source, band + 1), pixels[band], src_crs=source.crs,
                      src_transform=source.transform, dst_crs=frame["crs"], dst_transform=transform,
                      resampling=Resampling.bilinear)
    return Image.fromarray(np.moveaxis(pixels, 0, -1))


def polygon_parts(geometry):
    if geometry.is_empty:
        return
    if geometry.geom_type == "Polygon":
        yield geometry
    elif hasattr(geometry, "geoms"):
        for part in geometry.geoms:
            yield from polygon_parts(part)


def svg_path(geometry, bounds) -> str:
    """Encode north-up polygons with holes in a 1,000-unit SVG map."""
    x0, y0, x1, y1 = bounds
    scale = 1000 / (x1 - x0)
    parts = []
    for polygon in polygon_parts(geometry):
        for ring in [polygon.exterior, *polygon.interiors]:
            points = [f"{(x-x0)*scale:.3f},{(y1-y)*scale:.3f}" for x, y in ring.coords]
            parts.append("M" + "L".join(points) + "Z")
    return "".join(parts)


def file_sha256(path: Path) -> str:
    with Path(path).open("rb") as stream:
        return hashlib.file_digest(stream, "sha256").hexdigest()


def load_tree_points(path: Path, forest: dict) -> list[list[float]]:
    """Read authoritative column-major tree translations as local east/north."""
    document = json.loads(Path(path).read_text(encoding="utf-8"))
    if document.get("schema") != "godollo.tree-instances" or document.get("version") != 1:
        raise ValueError("Unsupported tree placement schema/version")
    metadata, frame = document["map"], forest["coordinate_frame"]
    if (metadata["map_sha256"] != forest["source"]["map_sha256"]
            or CRS.from_user_input(metadata["crs"]) != CRS.from_user_input(frame["crs"])
            or metadata["units"] != "meters"
            or not np.allclose(metadata["origin_projected_m"], frame["origin_projected_m"], rtol=0, atol=1e-6)):
        raise ValueError("Tree placements do not match the forest map/frame")
    transforms = document["transforms"]
    if (not transforms.get("matrix_authoritative") or transforms.get("units") != "meters"
            or not transforms.get("matrix_layout", "").startswith("column-major")
            or transforms.get("axes") != {"X": "east", "Y": "up", "Z": "south"}):
        raise ValueError("Tree placements require authoritative column-major east/up/south matrices")
    result = []
    for instance in document["instances"]:
        matrix = np.asarray(instance["matrix"], dtype=float)
        if (matrix.shape != (16,) or not np.isfinite(matrix).all()
                or not np.allclose(matrix[[3, 7, 11, 15]], [0, 0, 0, 1], rtol=0, atol=1e-9)):
            raise ValueError("Invalid tree instance matrix")
        result.append([float(matrix[12]), float(-matrix[14])])
    return result
