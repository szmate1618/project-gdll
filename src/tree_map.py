"""Read tree placement elevations from the published map, never raster caches."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path
import struct

import numpy as np
from pyproj import CRS

from .config import coordinate_frame
from .terrain import Terrain


_GLTF_FROM_LOCAL = np.array([[1., 0., 0., 0.], [0., 0., 1., 0.],
                             [0., -1., 0., 0.], [0., 0., 0., 1.]])
_AXES = {"X": "projected east", "Y": "up", "Z": "projected south"}


class _RenderedTerrain(Terrain):
    """Use exported node coordinates, including their float32 grid rounding."""

    def __init__(self, heights, x_axis, y_axis, datum):
        super().__init__(heights, (x_axis[0], y_axis[-1], x_axis[-1], y_axis[0]), datum)
        self.x_axis = x_axis
        self.y_axis = y_axis

    def __call__(self, x, y):
        x, y = np.broadcast_arrays(np.asarray(x, dtype=float), np.asarray(y, dtype=float))
        x = np.clip(x, self.x_axis[0], self.x_axis[-1])
        y = np.clip(y, self.y_axis[-1], self.y_axis[0])
        i = np.clip(np.searchsorted(self.x_axis, x, side="right") - 1, 0, self.cols - 2)
        j = np.clip(np.searchsorted(-self.y_axis, -y, side="right") - 1, 0, self.rows - 2)
        u = (x - self.x_axis[i]) / (self.x_axis[i + 1] - self.x_axis[i])
        v = (self.y_axis[j] - y) / (self.y_axis[j] - self.y_axis[j + 1])
        nw, ne = self.heights[j, i], self.heights[j, i + 1]
        sw, se = self.heights[j + 1, i], self.heights[j + 1, i + 1]
        value = np.where(v >= u, nw * (1 - v) + sw * (v - u) + se * u,
                         nw * (1 - u) + se * v + ne * (u - v))
        return float(value) if value.ndim == 0 else value


def _integer(value, name, minimum=0):
    if isinstance(value, bool) or not isinstance(value, int) or value < minimum:
        raise ValueError(f"Map {name} must be an integer >= {minimum}.")
    return value


def _item(sequence, index, name):
    index = _integer(index, f"{name} index")
    if index >= len(sequence):
        raise ValueError(f"Map {name} index is out of range.")
    return sequence[index]


def _document(stream):
    size = os.fstat(stream.fileno()).st_size
    header = stream.read(12)
    if len(header) != 12 or struct.unpack("<4sII", header) != (b"glTF", 2, size):
        raise ValueError("Tree placement requires a complete GLB 2.0 map.")
    document = binary = None
    while stream.tell() < size:
        chunk = stream.read(8)
        if len(chunk) != 8:
            raise ValueError("Map contains a truncated GLB chunk header.")
        length, kind = struct.unpack("<I4s", chunk)
        start = stream.tell()
        if length % 4 or start + length > size:
            raise ValueError("Map contains an invalid GLB chunk length.")
        if document is None and kind != b"JSON":
            raise ValueError("Map GLB must begin with its JSON chunk.")
        if kind == b"JSON":
            if document is not None:
                raise ValueError("Map contains duplicate JSON chunks.")
            document = json.loads(stream.read(length))
        elif kind == b"BIN\0":
            if binary is not None:
                raise ValueError("Map contains duplicate binary chunks.")
            binary = (start, length)
        stream.seek(start + length)
    if document is None or binary is None or document.get("asset", {}).get("version") != "2.0":
        raise ValueError("Map is missing its glTF 2.0 document or embedded terrain buffer.")
    buffers = document.get("buffers", [])
    if len(buffers) != 1 or "uri" in buffers[0]:
        raise ValueError("Map must use one embedded GLB buffer.")
    byte_length = _integer(buffers[0]["byteLength"], "buffer size", 1)
    if not byte_length <= binary[1] <= byte_length + 3:
        raise ValueError("Map buffer size does not match its binary chunk.")
    return document, (binary[0], byte_length)


def _accessor(stream, document, binary, index, *, positions=False):
    accessor = _item(document["accessors"], index, "accessor")
    types = {5126: np.dtype("<f4")} if positions else {
        5121: np.dtype("u1"), 5123: np.dtype("<u2"), 5125: np.dtype("<u4")}
    dtype = types.get(accessor.get("componentType"))
    components = 3 if positions else 1
    if (dtype is None or accessor.get("type") != ("VEC3" if positions else "SCALAR")
            or "sparse" in accessor or accessor.get("normalized", False)):
        raise ValueError("Map terrain needs dense float32 positions and unsigned triangle indices.")
    view = _item(document["bufferViews"], accessor["bufferView"], "buffer view")
    if view.get("buffer", 0) != 0:
        raise ValueError("Map terrain references an external buffer.")
    offset = _integer(accessor.get("byteOffset", 0), "accessor offset")
    view_offset = _integer(view.get("byteOffset", 0), "buffer view offset")
    view_length = _integer(view["byteLength"], "buffer view length", 1)
    count = _integer(accessor["count"], "accessor count", 1)
    packed = components * dtype.itemsize
    stride = _integer(view.get("byteStride", packed), "accessor stride", packed)
    length = (count - 1) * stride + packed
    if (stride % dtype.itemsize or offset % dtype.itemsize or view_offset % dtype.itemsize
            or offset + length > view_length or view_offset + view_length > binary[1]):
        raise ValueError("Map terrain accessor extends outside its buffer or is misaligned.")
    stream.seek(binary[0] + view_offset + offset)
    raw = stream.read(length)
    if len(raw) != length:
        raise ValueError("Map terrain data is truncated.")
    return np.ndarray((count, components), dtype=dtype, buffer=raw,
                      strides=(stride, dtype.itemsize)).copy()


def _terrain_primitive(document, scene):
    nodes, meshes = document["nodes"], document["meshes"]
    pending = [(index, np.eye(4)) for index in scene["nodes"]]
    seen, terrain = set(), []
    while pending:
        index, parent = pending.pop()
        node = _item(nodes, index, "node")
        if index in seen:
            raise ValueError("Map scene contains repeated or cyclic nodes.")
        seen.add(index)
        if any(key in node for key in ("translation", "rotation", "scale")):
            raise ValueError("Map must retain the generator's matrix node transforms.")
        matrix = np.asarray(node.get("matrix", np.eye(4).ravel()), dtype=float)
        if matrix.size != 16 or not np.isfinite(matrix).all():
            raise ValueError("Map contains an invalid node transform.")
        world = parent @ matrix.reshape(4, 4).T
        pending.extend((child, world) for child in node.get("children", []))
        if "mesh" not in node:
            continue
        mesh = _item(meshes, node["mesh"], "mesh")
        if mesh.get("name") != "terrain":
            continue
        if not np.allclose(world, _GLTF_FROM_LOCAL, rtol=0, atol=1e-12):
            raise ValueError("Map terrain transform does not match east/north/up to glTF Y-up axes.")
        if "skin" in node or node.get("weights") or mesh.get("weights"):
            raise ValueError("Map terrain must be static.")
        primitives = mesh["primitives"]
        if len(primitives) != 1:
            raise ValueError("Map terrain must contain one regular height-grid primitive.")
        primitive = primitives[0]
        if primitive.get("mode", 4) != 4 or primitive.get("targets") or primitive.get("extensions"):
            raise ValueError("Map terrain must use unmodified triangle geometry.")
        terrain.append(primitive)
    if len(terrain) != 1:
        raise ValueError("Map must contain exactly one active terrain mesh.")
    return terrain[0]


def _same_vector(metadata, key, expected, tolerance):
    value = np.asarray(metadata.get(key), dtype=float)
    if value.shape != np.shape(expected) or not np.allclose(value, expected, rtol=0, atol=tolerance):
        raise ValueError(f"Map {key} does not match the current config; regenerate the map first.")


def load_tree_map(map_path: Path, config: dict) -> tuple[Terrain, dict, dict]:
    """Load published float32 terrain and its fingerprint for a validated config.

    ``config`` is the result of :func:`src.config.load_config`. Returned terrain
    takes local east/north meters and gives local elevation (glTF Y), without
    adding its absolute elevation datum. No building meshes or textures load.
    """
    frame = coordinate_frame(config)
    with Path(map_path).open("rb") as stream:
        original_stat = os.fstat(stream.fileno())
        try:
            document, binary = _document(stream)
            if document.get("animations"):
                raise ValueError("Tree placement requires a static generated map.")
            scene = _item(document["scenes"], document.get("scene", 0), "scene")
            source_metadata = scene["extras"]
            _same_vector(source_metadata, "bbox_wgs84", config["bbox"], 1e-10)
            if CRS.from_user_input(source_metadata["crs"]) != CRS.from_user_input(frame["crs"]):
                raise ValueError("Map crs does not match the current config; regenerate the map first.")
            _same_vector(source_metadata, "origin_wgs84", frame["center"], 1e-10)
            _same_vector(source_metadata, "origin_projected_m", frame["origin"], 1e-6)
            if source_metadata.get("units") != "meters" or source_metadata.get("gltf_axes") != _AXES:
                raise ValueError("Map metadata must describe the generator's meter and Y-up coordinate frame.")
            datum = float(source_metadata["origin_elevation_m"])
            if not np.isfinite(datum):
                raise ValueError("Map elevation datum must be finite.")
            cols, rows = source_metadata["terrain"]["grid_vertices"]
            cols, rows = _integer(cols, "terrain columns", 2), _integer(rows, "terrain rows", 2)
            primitive = _terrain_primitive(document, scene)
            positions = _accessor(stream, document, binary, primitive["attributes"]["POSITION"], positions=True)
            indices = _accessor(stream, document, binary, primitive["indices"]).ravel()
            if len(positions) != rows * cols or not np.isfinite(positions).all():
                raise ValueError("Map terrain positions do not match a finite height grid.")
            grid = positions.reshape(rows, cols, 3)
            xmin, ymin, xmax, ymax = frame["bounds"]
            x_axis = np.linspace(xmin, xmax, cols).astype(np.float32)
            y_axis = np.linspace(ymax, ymin, rows).astype(np.float32)
            if (not np.all(grid[:, :, 0] == x_axis[None, :])
                    or not np.all(grid[:, :, 1] == y_axis[:, None])
                    or not np.all(np.diff(x_axis) > 0) or not np.all(np.diff(y_axis) < 0)):
                raise ValueError("Map terrain grid does not match the configured projected bounds.")
            vertices = np.arange(rows * cols).reshape(rows, cols)
            nw, ne, sw, se = (g.ravel() for g in (vertices[:-1, :-1], vertices[:-1, 1:],
                                                 vertices[1:, :-1], vertices[1:, 1:]))
            expected = np.vstack((np.column_stack((nw, sw, se)), np.column_stack((nw, se, ne)))).ravel()
            if not np.array_equal(indices, expected):
                raise ValueError("Map terrain topology must use the generator's NW-to-SE grid triangles.")
            terrain = _RenderedTerrain(grid[:, :, 2].astype(float), x_axis.astype(float), y_axis.astype(float), datum)
            stream.seek(0)
            digest = hashlib.file_digest(stream, "sha256").hexdigest()
        except (KeyError, IndexError, TypeError, struct.error) as error:
            raise ValueError(f"Map lacks valid generated terrain metadata or geometry: {error}") from error
        current_stat = os.fstat(stream.fileno())
        if (current_stat.st_size, current_stat.st_mtime_ns) != (original_stat.st_size, original_stat.st_mtime_ns):
            raise ValueError("Map changed while reading tree elevations; retry with the completed map.")
    metadata = {key: source_metadata[key] for key in ("bbox_wgs84", "crs", "origin_wgs84", "origin_projected_m",
                "origin_elevation_m", "units", "gltf_axes")}
    metadata.update(map_sha256=digest, terrain_bounds_m=list(terrain.bounds), terrain_grid_vertices=[cols, rows])
    return terrain, frame, metadata
