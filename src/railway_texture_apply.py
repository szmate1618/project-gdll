"""Apply railway artwork to a published GLB without regenerating geometry."""
from __future__ import annotations

import copy
import hashlib
import json
from pathlib import Path
import shutil
import tempfile

import numpy as np
import trimesh

from .glb import read_glb as _read_glb, pack_glb as _pack_glb
from .osm import parse_railways
from .railways import add_railways
from .railway_texture import GAUGE_M, TIE_SPACING_M, TEXTURE_PATH, configure_track_sampler
from .tree_map import load_tree_map
from .verify import inspect_glb


def _accessor(document, binary, index):
    spec = document["accessors"][index]
    if "sparse" in spec or spec.get("normalized"):
        raise ValueError("Railway patch requires dense, unnormalized accessors")
    dtype = {5126: np.dtype("<f4"), 5123: np.dtype("<u2"), 5125: np.dtype("<u4")}[spec["componentType"]]
    count = spec["count"]
    components = {"VEC3": 3, "SCALAR": 1}[spec["type"]]
    view = document["bufferViews"][spec["bufferView"]]
    start = view.get("byteOffset", 0) + spec.get("byteOffset", 0)
    stride = view.get("byteStride", components * dtype.itemsize)
    size = (count - 1) * stride + components * dtype.itemsize
    if (view.get("buffer", 0) != 0 or count <= 0 or stride < components * dtype.itemsize
            or start < 0 or start + size > view.get("byteOffset", 0) + view["byteLength"]
            or start + size > len(binary)):
        raise ValueError("Railway accessor extends outside its buffer view")
    return np.ndarray((count, components), dtype=dtype, buffer=binary,
                       offset=start, strides=(stride, dtype.itemsize)).copy()


def prepare_texture_patch(original: bytes, generated: trimesh.Trimesh) -> bytes:
    """Append UV/image data while preserving every existing binary byte.

    A reconstructed railway bed must exactly match the published XY positions,
    triangle indices, and source IDs. Its reconstructed heights are unused:
    the original positions, terrain, buildings, and scene transforms are kept.
    """
    document, original_binary = _read_glb(original)
    matches = [mesh for mesh in document["meshes"] if mesh.get("name") == "railways_ballast"]
    if len(matches) != 1 or len(matches[0]["primitives"]) != 1:
        raise ValueError("Expected one published railways_ballast primitive")
    mesh = matches[0]
    primitive = mesh["primitives"][0]
    positions = _accessor(document, original_binary, primitive["attributes"]["POSITION"])
    indices = _accessor(document, original_binary, primitive["indices"]).ravel()
    if (not np.array_equal(positions[:, :2], generated.vertices[:, :2].astype(np.float32))
            or not np.array_equal(indices, generated.faces.ravel())
            or mesh.get("extras", {}).get("osm_ids") != generated.metadata.get("osm_ids")):
        raise ValueError("Cached railway geometry/config differs from the published map; refusing to patch")
    uv = np.asarray(generated.visual.uv, dtype="<f4").copy()
    # Match trimesh's glTF export convention for Pillow's image row direction.
    uv[:, 1] = 1.0 - uv[:, 1]
    if uv.shape != (len(positions), 2) or not np.isfinite(uv).all():
        raise ValueError("Railway UVs must match all finite published vertices")
    binary = bytearray(original_binary)

    def append_view(data, target=None):
        binary.extend(b"\0" * (-len(binary) % 4))
        view = {"buffer": 0, "byteOffset": len(binary), "byteLength": len(data)}
        if target is not None:
            view["target"] = target
        document["bufferViews"].append(view)
        binary.extend(data)
        return len(document["bufferViews"]) - 1

    view = append_view(uv.tobytes(), 34962)
    document["accessors"].append({"bufferView": view, "componentType": 5126, "type": "VEC2",
                                  "count": len(uv), "min": uv.min(axis=0).tolist(), "max": uv.max(axis=0).tolist()})
    primitive["attributes"]["TEXCOORD_0"] = len(document["accessors"]) - 1
    image_view = append_view(TEXTURE_PATH.read_bytes())
    images = document.setdefault("images", [])
    images.append({"name": "metric railway track", "bufferView": image_view, "mimeType": "image/png"})
    textures = document.setdefault("textures", [])
    textures.append({"source": len(images) - 1})
    # A separate material avoids changing any other draw that shares the old one.
    material = copy.deepcopy(document["materials"][primitive["material"]])
    pbr = material.setdefault("pbrMetallicRoughness", {})
    pbr.update(baseColorFactor=[1, 1, 1, 1], baseColorTexture={"index": len(textures) - 1}, roughnessFactor=0.85)
    document["materials"].append(material)
    primitive["material"] = len(document["materials"]) - 1
    configure_track_sampler(document)
    mesh.setdefault("extras", {})["track_texture"] = {"gauge_m": GAUGE_M, "tie_spacing_m": TIE_SPACING_M}
    document["buffers"][0]["byteLength"] = len(binary)
    assert bytes(binary[:len(original_binary)]) == original_binary
    return _pack_glb(document, bytes(binary))


def apply_railway_texture(map_path: Path, config: dict) -> dict:
    """Offline material/UV update, with validated temporary files and backups.

    Refresh an adjacent tree manifest hash only after proving all geometry and
    original binary bytes are unchanged. A stale manifest is rejected, never
    re-bound. Backups also support rollback if companion publication fails.
    """
    map_path = Path(map_path).resolve()
    original = map_path.read_bytes()
    old_hash = hashlib.sha256(original).hexdigest()
    _, frame, _ = load_tree_map(map_path, config)
    document, _ = _read_glb(original)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    osm_path = Path(metadata["sources"]["osm"]["path"])
    if not osm_path.is_absolute():
        osm_path = Path(__file__).resolve().parents[1] / osm_path
    payload = json.loads(osm_path.read_text())
    features = parse_railways(payload, frame["transformer"], frame["origin"], frame["clip"])
    scene = trimesh.Scene()
    add_railways(scene, features, lambda x, y: 0, {**config, "_clip_polygon": frame["clip"]})
    if "railways_ballast" not in scene.geometry:
        raise ValueError("The cached OSM extract contains no usable railway beds")
    patched = prepare_texture_patch(original, scene.geometry["railways_ballast"])
    new_hash = hashlib.sha256(patched).hexdigest()
    companions = {}
    tree_path = map_path.with_name("trees.instances.json")
    if tree_path.exists():
        tree_bytes = tree_path.read_bytes()
        tree = json.loads(tree_bytes)
        if tree["map"]["map_sha256"] != old_hash:
            raise ValueError("Tree manifest does not match the current map; refusing to refresh its fingerprint")
        tree["map"]["map_sha256"] = new_hash
        companions[tree_path] = (tree_bytes, (json.dumps(tree, indent=2, ensure_ascii=False) + "\n").encode())
    pending = {}
    backup = map_path.parent / "railway-texture-backups" / old_hash

    def stage(path, data):
        with tempfile.NamedTemporaryFile(prefix=".railway-texture-", suffix=path.suffix,
                                         dir=path.parent, delete=False) as stream:
            stream.write(data)
            return Path(stream.name)

    try:
        pending[map_path] = stage(map_path, patched)
        verification = inspect_glb(pending[map_path])
        report_path = map_path.with_name("report.json")
        if report_path.exists():
            report_bytes = report_path.read_bytes()
            report = json.loads(report_bytes)
            report["verification"] = verification
            report.setdefault("railways", {})["track_texture"] = {"gauge_m": GAUGE_M, "tie_spacing_m": TIE_SPACING_M}
            companions[report_path] = (report_bytes, (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode())
        for path, (_, replacement) in companions.items():
            pending[path] = stage(path, replacement)
        originals = {map_path: original, **{path: pair[0] for path, pair in companions.items()}}
        if any(path.read_bytes() != data for path, data in originals.items()):
            raise ValueError("Map or companion changed during preparation; retry after that work completes")
        backup.mkdir(parents=True, exist_ok=True)
        for path, data in originals.items():
            saved = backup / path.name
            if saved.exists() and saved.read_bytes() != data:
                raise ValueError("An existing backup differs; refusing to overwrite it")
            if not saved.exists():
                shutil.copy2(path, saved)
        published = []
        try:
            for path, temporary in pending.items():
                temporary.replace(path)
                published.append(path)
        except Exception:
            for path in reversed(published):
                stage(path, originals[path]).replace(path)
            raise
    finally:
        for temporary in pending.values():
            temporary.unlink(missing_ok=True)
    return {"map": str(map_path), "backup": str(backup), "gauge_m": GAUGE_M,
            "tie_spacing_m": TIE_SPACING_M, "railway_vertices": len(scene.geometry["railways_ballast"].vertices),
            "map_sha256": new_hash, "tree_fingerprint_refreshed": tree_path in companions}
