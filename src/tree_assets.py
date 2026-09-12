"""Read calibrated tree impostor footprints without decoding or altering images."""

from __future__ import annotations

import hashlib
import json
import math
from pathlib import Path
import struct


_PLANES = (("front", "XY"), ("side", "YZ"), ("top-mid", "XZ"), ("top-two-thirds", "XZ"))


def _number(value, label: str) -> float:
    if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value):
        raise ValueError(f"{label} must be a finite number")
    return float(value)


def _vector(value, size: int, label: str) -> list[float]:
    if not isinstance(value, list) or len(value) != size:
        raise ValueError(f"{label} must contain {size} numbers")
    return [_number(component, label) for component in value]


def _union(bounds: list[dict]) -> dict:
    return {"min": [min(item["min"][axis] for item in bounds) for axis in range(3)],
            "max": [max(item["max"][axis] for item in bounds) for axis in range(3)]}


def _card_bounds(index: int, rectangle: list[float], pivot: list[float],
                 scale: float, plane_height: float) -> dict:
    left, top, right, bottom = rectangle
    x0, x1 = (left - pivot[0]) * scale, (right - pivot[0]) * scale
    y0, y1 = (top - pivot[1]) * scale, (bottom - pivot[1]) * scale
    if index == 0:
        return {"min": [x0, -y1, 0.0], "max": [x1, -y0, 0.0]}
    if index == 1:
        # The right-side texture looks toward -X; image right maps toward -Z.
        return {"min": [0.0, -y1, -x1], "max": [0.0, -y0, -x0]}
    return {"min": [x0, plane_height, y0], "max": [x1, plane_height, y1]}


def _close(first: float, second: float) -> bool:
    return math.isclose(first, second, rel_tol=1e-6, abs_tol=1e-6)


def _measure(document: dict) -> dict:
    metadata = document["extras"]
    if metadata["units"] != "meters" or metadata["upAxis"] != "+Y":
        raise ValueError("impostor metadata must use meters and +Y up")
    if metadata["origin"] != "visible trunk base":
        raise ValueError("impostor origin must be the visible trunk base")
    height = _number(metadata["visibleHeightMeters"], "visibleHeightMeters")
    if height <= 0:
        raise ValueError("visibleHeightMeters must be positive")
    threshold = metadata["alphaBoundsThreshold"]
    if type(threshold) is not int or not 1 <= threshold <= 255:
        raise ValueError("alphaBoundsThreshold must be an integer from 1 to 255")
    sources, planes = metadata["sourceTextures"], metadata["planes"]
    if not isinstance(sources, list) or not isinstance(planes, list) or len(sources) != 4 or len(planes) != 4:
        raise ValueError("impostor metadata must describe exactly four texture planes")
    meshes, nodes = document["meshes"], document["nodes"]
    if len(meshes) != 1 or len(nodes) != 1 or nodes[0].get("mesh") != 0:
        raise ValueError("expected a single impostor mesh and node")
    if any(field in nodes[0] for field in ("matrix", "translation", "rotation", "scale", "children")):
        raise ValueError("impostor node transforms are unsupported; bake them into the calibrated asset")
    primitives = meshes[0]["primitives"]
    if len(primitives) != 4:
        raise ValueError("impostor mesh must have four quad primitives")

    visible_bounds, geometry_bounds, horizontal_calibration = [], [], []
    front_scale = None
    for index, (source, plane, primitive, expected) in enumerate(zip(sources, planes, primitives, _PLANES)):
        name, orientation = expected
        if plane["name"] != name or plane["plane"] != orientation:
            raise ValueError(f"plane {index} must be {name} in {orientation}")
        if primitive.get("extras", {}).get("name") != name or primitive.get("mode", 4) != 4:
            raise ValueError(f"primitive {index} does not match its plane metadata")
        canvas = _vector(source["canvasPixels"], 2, f"{name} canvasPixels")
        rectangle = _vector(source["visibleBoundsPixels"], 4, f"{name} visibleBoundsPixels")
        pivot = _vector(source["pivotPixels"], 2, f"{name} pivotPixels")
        scale = _number(source["metersPerPixel"], f"{name} metersPerPixel")
        if min(canvas) <= 0 or any(int(dimension) != dimension for dimension in canvas) or scale <= 0:
            raise ValueError(f"{name} requires positive integer canvas dimensions and positive scale")
        left, top, right, bottom = rectangle
        if not (0 <= left < right <= canvas[0] and 0 <= top < bottom <= canvas[1]):
            raise ValueError(f"{name} visible bounds must be nonempty and inside the canvas")
        if not all(0 <= pivot[axis] <= canvas[axis] for axis in range(2)):
            raise ValueError(f"{name} pivot must lie within the canvas")
        plane_height = 0.0
        if index < 2:
            if not _close((bottom - top) * scale, height) or not _close(pivot[1], bottom):
                raise ValueError(f"{name} calibration does not match visible height and trunk-base origin")
            if index == 0:
                front_scale = scale
        else:
            fraction = 0.5 if index == 2 else 2 / 3
            plane_height = _number(plane["heightMeters"], f"{name} heightMeters")
            if not _close(_number(plane["heightFraction"], f"{name} heightFraction"), fraction):
                raise ValueError(f"{name} has an incorrect slice height fraction")
            if not _close(plane_height, height * fraction):
                raise ValueError(f"{name} slice height does not match visible height")
            if not _close(scale, front_scale) or not all(_close(pivot[axis], canvas[axis] / 2) for axis in range(2)):
                raise ValueError(f"{name} must use the front scale and centered canvas pivot")
            horizontal_calibration.append(canvas)

        position_index = primitive["attributes"]["POSITION"]
        if type(position_index) is not int or not 0 <= position_index < len(document["accessors"]):
            raise ValueError(f"{name} has an invalid POSITION accessor")
        position = document["accessors"][position_index]
        if position["type"] != "VEC3" or position["componentType"] != 5126 or position["count"] != 4:
            raise ValueError(f"{name} POSITION accessor must contain four float32 VEC3 vertices")
        actual = {key: _vector(position[key], 3, f"{name} POSITION {key}") for key in ("min", "max")}
        expected_bounds = _card_bounds(index, [0, 0, *canvas], pivot, scale, plane_height)
        if not all(_close(actual[key][axis], expected_bounds[key][axis])
                   for key in ("min", "max") for axis in range(3)):
            raise ValueError(f"{name} POSITION bounds disagree with canvas calibration")
        geometry_bounds.append(actual)
        visible_bounds.append(_card_bounds(index, rectangle, pivot, scale, plane_height))
    if horizontal_calibration[0] != horizontal_calibration[1]:
        raise ValueError("horizontal slices must share the same canvas dimensions")
    visible = _union(visible_bounds)
    footprint = [visible["max"][axis] - visible["min"][axis] for axis in (0, 2)]
    if not all(math.isfinite(value) and value > 0 for value in footprint):
        raise ValueError("impostor footprint must have finite positive width and depth")
    return {"visible_height_m": height, "visible_bounds_m": visible,
            "geometry_bounds_m": _union(geometry_bounds), "footprint_m": footprint}


def load_impostor(path: Path) -> dict:
    """Return an impostor's calibrated alpha footprint and padded geometry bounds.

    Alpha bounds come from the packager's measured metadata, unioned across all
    four planes. Geometry bounds retain transparent margins for future culling.
    PNGs stay encoded. Returned paths are absolute and SHA-256 covers the full
    file. Invalid or unsupported asset metadata raises a contextual ValueError.
    """
    path = Path(path).expanduser().resolve()
    try:
        with path.open("rb") as stream:
            header = stream.read(20)
            if len(header) != 20:
                raise ValueError("truncated GLB header")
            magic, version, length, json_length, kind = struct.unpack("<4sIII4s", header)
            if magic != b"glTF" or version != 2 or kind != b"JSON" or json_length % 4:
                raise ValueError("expected a glTF 2.0 binary with an aligned JSON chunk")
            if length != path.stat().st_size or json_length > length - 20:
                raise ValueError("GLB file or JSON chunk length is inconsistent")
            encoded = stream.read(json_length)
            document = json.loads(encoded)
            digest = hashlib.sha256(header + encoded)
            for block in iter(lambda: stream.read(1024 * 1024), b""):
                digest.update(block)
        result = _measure(document)
        return {"path": str(path), "sha256": digest.hexdigest(), **result}
    except (OSError, UnicodeError, json.JSONDecodeError, KeyError, IndexError, TypeError,
            ValueError, OverflowError, struct.error) as error:
        raise ValueError(f"Cannot read tree impostor {path}: {error}") from error
