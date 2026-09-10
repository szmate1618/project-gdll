"""Reproject local raster sources and construct a textured terrain heightfield."""
from __future__ import annotations

import logging
import math
from pathlib import Path

import numpy as np
from PIL import Image
import rasterio
from rasterio.transform import from_bounds, from_origin
from rasterio.warp import Resampling, reproject
from scipy.ndimage import distance_transform_edt, gaussian_filter, median_filter
import trimesh

LOG = logging.getLogger(__name__)


def fill_missing(array: np.ndarray, valid: np.ndarray, name: str) -> np.ndarray:
    """Fill sparse holes with nearest valid pixels; never invent an absent dataset."""
    missing = 1 - np.count_nonzero(valid) / valid.size
    if missing > .25:
        raise ValueError(f"{name}: {missing:.1%} missing pixels; choose another scene/bbox or refresh source cache.")
    result = np.array(array, copy=True)
    if missing:
        LOG.warning("%s: filling %.2f%% missing pixels with nearest valid data", name, missing * 100)
        nearest = distance_transform_edt(~valid, return_distances=False, return_indices=True)
        result[~valid] = result[tuple(nearest[:, ~valid])]
    return result


class Terrain:
    """North-to-south height grid with sampling of the actual rendered triangles."""

    def __init__(self, heights: np.ndarray, bounds: tuple, datum: float = 0):
        self.heights = np.asarray(heights, dtype=np.float64)
        self.bounds = tuple(bounds)
        self.datum = float(datum)
        self.rows, self.cols = self.heights.shape
        self.dx = (bounds[2] - bounds[0]) / (self.cols - 1)
        self.dy = (bounds[3] - bounds[1]) / (self.rows - 1)

    def __call__(self, x, y):
        x, y = np.broadcast_arrays(np.asarray(x, dtype=float), np.asarray(y, dtype=float))
        col = np.clip((x - self.bounds[0]) / self.dx, 0, self.cols - 1)
        row = np.clip((self.bounds[3] - y) / self.dy, 0, self.rows - 1)
        i = np.minimum(col.astype(int), self.cols - 2)
        j = np.minimum(row.astype(int), self.rows - 2)
        u, v = col - i, row - j
        nw = self.heights[j, i]
        ne = self.heights[j, i + 1]
        sw = self.heights[j + 1, i]
        se = self.heights[j + 1, i + 1]
        # The mesh diagonal connects NW to SE. These barycentric planes exactly
        # match the mesh, including saddle-shaped cells where bilinear does not.
        value = np.where(v >= u, nw * (1 - v) + sw * (v - u) + se * u,
                         nw * (1 - u) + se * v + ne * (u - v))
        return float(value) if value.ndim == 0 else value

    def mesh(self, texture: Image.Image) -> trimesh.Trimesh:
        x = np.linspace(self.bounds[0], self.bounds[2], self.cols)
        y = np.linspace(self.bounds[3], self.bounds[1], self.rows)
        xx, yy = np.meshgrid(x, y)
        vertices = np.column_stack((xx.ravel(), yy.ravel(), self.heights.ravel()))
        grid = np.arange(self.rows * self.cols).reshape(self.rows, self.cols)
        nw, ne, sw, se = (g.ravel() for g in (grid[:-1, :-1], grid[:-1, 1:], grid[1:, :-1], grid[1:, 1:]))
        faces = np.vstack((np.column_stack((nw, sw, se)), np.column_stack((nw, se, ne))))
        uv = np.column_stack(((xx.ravel() - self.bounds[0]) / (self.bounds[2] - self.bounds[0]),
                              (yy.ravel() - self.bounds[1]) / (self.bounds[3] - self.bounds[1])))
        material = trimesh.visual.material.PBRMaterial(
            name="terrain_sentinel_rgb", baseColorTexture=texture,
            baseColorFactor=[255, 255, 255, 255], metallicFactor=0, roughnessFactor=1,
        )
        return trimesh.Trimesh(vertices=vertices, faces=faces, process=False,
                               visual=trimesh.visual.TextureVisuals(uv=uv, material=material))


def build_terrain(config: dict, frame: dict, sources: dict, root: Path) -> tuple[Terrain, trimesh.Trimesh, dict]:
    bounds = frame["bounds"]
    xmin, ymin, xmax, ymax = bounds
    spacing = config["terrain"]["spacing_m"]
    cols, rows = math.ceil((xmax - xmin) / spacing) + 1, math.ceil((ymax - ymin) / spacing) + 1
    dx, dy = (xmax - xmin) / (cols - 1), (ymax - ymin) / (rows - 1)
    ox, oy = frame["origin"]
    # Raster pixels represent mesh nodes, so the transform includes half a cell.
    transform = from_origin(ox + xmin - dx / 2, oy + ymax + dy / 2, dx, dy)
    heights = np.full((rows, cols), np.nan, dtype=np.float32)
    LOG.info("Reprojecting Copernicus elevation to %d × %d terrain vertices", cols, rows)
    for path in sources["dem"]:
        with rasterio.open(path) as src:
            part = np.full_like(heights, np.nan)
            reproject(rasterio.band(src, 1), part, src_transform=src.transform, src_crs=src.crs,
                      src_nodata=src.nodata, dst_transform=transform, dst_crs=frame["crs"],
                      dst_nodata=np.nan, resampling=Resampling.bilinear)
            valid = np.isfinite(part) & (part > -500) & (part < 9000)
            heights[valid] = part[valid]
    valid = np.isfinite(heights)
    missing_count = int(np.count_nonzero(~valid))
    heights = fill_missing(heights, valid, "Copernicus DEM")
    # Only replace severe isolated spikes, retaining ordinary hillside gradients.
    median = median_filter(heights, size=3, mode="nearest")
    spikes = np.abs(heights - median) > 40
    heights[spikes] = median[spikes]
    sigma = config["terrain"]["smoothing_sigma"]
    if sigma:
        heights = gaussian_filter(heights, sigma=sigma, mode="nearest")
    terrain = Terrain(heights, bounds)
    datum = terrain(0, 0)
    terrain.heights -= datum
    terrain.datum = datum
    processed = root / "data" / "processed"
    processed.mkdir(parents=True, exist_ok=True)
    with rasterio.open(processed / "terrain_elevation.tif", "w", driver="GTiff", width=cols, height=rows,
                       count=1, dtype="float32", crs=frame["crs"], transform=transform,
                       compress="deflate", nodata=-9999) as dst:
        dst.write(heights.astype(np.float32), 1)
        dst.update_tags(description="Absolute Copernicus elevation, EGM2008 meters; pixel centers are mesh nodes")
    np.savez_compressed(processed / "terrain.npz", heights=terrain.heights, bounds=bounds, datum=datum)
    texture = build_texture(config, frame, sources["sentinel"], processed)
    mesh = terrain.mesh(texture)
    stats = {
        "dimensions_m": [round(xmax - xmin, 3), round(ymax - ymin, 3)],
        "grid_vertices": [cols, rows], "vertices": len(mesh.vertices), "triangles": len(mesh.faces),
        "grid_spacing_m": [round(dx, 4), round(dy, 4)], "texture_pixels": list(texture.size),
        "elevation_absolute_m": [float(heights.min()), float(heights.max())],
        "elevation_local_m": [float(terrain.heights.min()), float(terrain.heights.max())],
        "origin_elevation_m": datum, "missing_pixels_filled": missing_count,
        "spikes_replaced": int(np.count_nonzero(spikes)),
    }
    return terrain, mesh, stats


def build_texture(config: dict, frame: dict, source: Path, processed: Path) -> Image.Image:
    size = config["terrain"]["texture_size"]
    xmin, ymin, xmax, ymax = frame["bounds"]
    ox, oy = frame["origin"]
    transform = from_bounds(ox + xmin, oy + ymin, ox + xmax, oy + ymax, size, size)
    rgb = np.zeros((3, size, size), dtype=np.uint8)
    mask = np.zeros((size, size), dtype=np.uint8)
    with rasterio.open(source) as src:
        if src.count < 3 or src.dtypes[0] != "uint8":
            raise ValueError("Expected Sentinel true-color RGB uint8 source raster.")
        for index in range(3):
            reproject(rasterio.band(src, index + 1), rgb[index], src_transform=src.transform,
                      src_crs=src.crs, dst_transform=transform, dst_crs=frame["crs"],
                      resampling=Resampling.bilinear)
        reproject(src.dataset_mask(), mask, src_transform=src.transform, src_crs=src.crs,
                  dst_transform=transform, dst_crs=frame["crs"], resampling=Resampling.nearest)
    for index in range(3):
        rgb[index] = fill_missing(rgb[index], mask > 0, "Sentinel RGB")
    with rasterio.open(processed / "sentinel_rgb.tif", "w", driver="GTiff", width=size, height=size,
                       count=3, dtype="uint8", crs=frame["crs"], transform=transform, compress="deflate") as dst:
        dst.write(rgb)
    image = Image.fromarray(np.moveaxis(rgb, 0, -1))
    image.save(processed / "terrain_texture.png")
    return image
