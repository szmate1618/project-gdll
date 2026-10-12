"""Query and atomically publish a fingerprinted guardrail GLB beside the town."""
from __future__ import annotations

import hashlib
import io
import json
import os
from pathlib import Path
import tempfile

import numpy as np
import requests
import trimesh

from .config import ROOT, load_config
from .data import _download_osm_query
from .glb import pack_glb, read_glb
from .guardrails import add_guardrails
from .osm import parse_guardrails
from .tree_map import load_tree_map


def guardrail_query(bbox):
    west, south, east, north = bbox
    return ('[out:json][timeout:90];\n'
            f'way["barrier"="guard_rail"]({south},{west},{north},{east});\n'
            'out body geom;\n')


def export_guardrail_scene(scene):
    if scene.geometry:
        return scene.export(file_type="glb")
    # trimesh refuses empty scenes; retain a valid GLB with no active geometry.
    return pack_glb({"asset": {"version": "2.0"}, "scene": 0,
                     "scenes": [{"nodes": [], "extras": scene.metadata}], "nodes": [], "meshes": [],
                     "buffers": [{"byteLength": 0}]}, b"")


def validate_guardrail_glb(data, map_hash):
    """Validate exported float32 geometry and embedded texture before publication."""
    document, _ = read_glb(data)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    if (metadata.get("schema") != "godollo.guardrails" or metadata.get("version") != 1
            or metadata.get("map_sha256") != map_hash):
        raise ValueError("Guardrail layer metadata does not match the map")
    if any("uri" in image for image in document.get("images", [])):
        raise ValueError("Guardrail texture must be embedded")
    scene = trimesh.load_scene(io.BytesIO(data), file_type="glb", process=False)
    triangles = 0
    for mesh in scene.geometry.values():
        if (not np.isfinite(mesh.vertices).all() or np.any(mesh.area_faces < 1e-10)
                or mesh.faces.min() < 0 or mesh.faces.max() >= len(mesh.vertices)):
            raise ValueError("Guardrail layer has invalid exported geometry")
        material = mesh.visual.material
        if material.alphaMode != "MASK" or not material.doubleSided or material.baseColorTexture is None:
            raise ValueError("Guardrails require textured, double-sided alpha masks")
        alpha = np.asarray(material.baseColorTexture)[:, :, 3]
        if not np.any(alpha == 0) or not np.any(alpha == 255):
            raise ValueError("Guardrail texture must retain transparent gaps and opaque rails/posts")
        triangles += len(mesh.faces)
    if triangles != metadata["guardrails"]["triangles"] or len(scene.geometry) != metadata["guardrails"]["batches"]:
        raise ValueError("Guardrail counts disagree with exported geometry")
    return metadata


def prepare_guardrails(*, config_path=ROOT / "config.json", map_path=ROOT / "output/godollo.glb",
                      output=None, offline=False, refresh=False, root=ROOT):
    config = load_config(Path(config_path))
    config.update(offline=offline, refresh=refresh)
    map_path = Path(map_path)
    output = Path(output) if output is not None else map_path.with_name("guardrails.glb")
    if output.resolve() == map_path.resolve():
        raise ValueError("Guardrail output must not overwrite the town")
    terrain, frame, map_info = load_tree_map(map_path, config)
    directory = Path(root) / "data/raw/osm"
    directory.mkdir(parents=True, exist_ok=True)
    with requests.Session() as session:
        session.headers["User-Agent"] = "godollo-guardrails/1.0 (OpenStreetMap scene generator)"
        path, provenance = _download_osm_query(config, directory, session, guardrail_query(config["bbox"]),
                                              "roadside guardrails", prefix="guardrails")
    features = parse_guardrails(json.loads(path.read_text(encoding="utf-8")),
                               frame["transformer"], frame["origin"], frame["clip"])
    scene = trimesh.Scene(base_frame="local_meters")
    stats = add_guardrails(scene, features, terrain)
    scene.graph.update(frame_from="world", frame_to="local_meters",
                       matrix=np.array([[1, 0, 0, 0], [0, 0, 1, 0], [0, -1, 0, 0], [0, 0, 0, 1]]))
    scene.graph.base_frame = "world"
    scene.metadata.update(schema="godollo.guardrails", version=1, map_sha256=map_info["map_sha256"],
                          map=map_info, source=provenance, guardrails=stats)
    data = export_guardrail_scene(scene)
    validate_guardrail_glb(data, map_info["map_sha256"])
    with map_path.open("rb") as stream:
        if hashlib.file_digest(stream, "sha256").hexdigest() != map_info["map_sha256"]:
            raise ValueError("Town changed during guardrail preparation; retry when it is complete")
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(prefix=output.name + ".", suffix=".glb", dir=output.parent,
                                         delete=False) as stream:
            temporary = Path(stream.name)
            stream.write(data)
            stream.flush()
            os.fsync(stream.fileno())
        validate_guardrail_glb(temporary.read_bytes(), map_info["map_sha256"])
        temporary.replace(output)
    finally:
        if temporary is not None:
            temporary.unlink(missing_ok=True)
    return {"output": str(output), **stats}
