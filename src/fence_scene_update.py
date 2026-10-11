"""Update fences in a published town without reconstructing its buildings."""
from __future__ import annotations

from datetime import datetime, timezone
import json
import logging
from pathlib import Path
import shutil
import tempfile
import time

import numpy as np
import trimesh

from .fences import add_fences
from .glb import pack_glb, read_glb
from .osm import parse_fences
from .tree_map import load_tree_map
from .verify import inspect_glb

LOG = logging.getLogger(__name__)


def _remove_fence_nodes(document: dict) -> None:
    """Remove previous fence draws, remapping the static town's graph indices."""
    old_meshes = document["meshes"]
    retained = {i: mesh for i, mesh in enumerate(old_meshes)
                if not mesh.get("name", "").startswith("fences_")}
    mesh_indices = {old: new for new, old in enumerate(retained)}
    nodes = {i: node for i, node in enumerate(document["nodes"])
             if "mesh" not in node or node["mesh"] in retained}
    node_indices = {old: new for new, old in enumerate(nodes)}
    for node in nodes.values():
        if "mesh" in node:
            node["mesh"] = mesh_indices[node["mesh"]]
        if "children" in node:
            node["children"] = [node_indices[i] for i in node["children"] if i in nodes]
    for scene in document["scenes"]:
        scene["nodes"] = [node_indices[i] for i in scene.get("nodes", []) if i in nodes]
    document["meshes"] = list(retained.values())
    document["nodes"] = list(nodes.values())


def prepare_fence_patch(original: bytes, fences: trimesh.Scene, stats: dict, provenance: dict) -> bytes:
    """Append fence buffers while retaining every original town binary byte.

    Existing fence draws are replaced. Their unused buffers remain in the GLB,
    just as with a material patch; no town accessor or material is reconstructed.
    The generator's map is static, so only mesh/node graph indices need remapping.
    """
    document, binary = read_glb(original)
    if document.get("skins") or document.get("animations"):
        raise ValueError("Fence updates require a static town without skins or animations")
    _remove_fence_nodes(document)
    if fences.geometry:
        generated, fence_binary = read_glb(fences.export(file_type="glb"))
        binary += b"\0" * (-len(binary) % 4)
        offset = len(binary)
        binary += fence_binary
        bases = {key: len(document.setdefault(key, [])) for key in
                 ("bufferViews", "accessors", "images", "textures", "samplers", "materials", "meshes")}
        for view in generated.get("bufferViews", []):
            view["buffer"] = 0
            view["byteOffset"] = view.get("byteOffset", 0) + offset
        for accessor in generated.get("accessors", []):
            accessor["bufferView"] += bases["bufferViews"]
        for image in generated.get("images", []):
            image["bufferView"] += bases["bufferViews"]
        for texture in generated.get("textures", []):
            texture["source"] += bases["images"]
            if "sampler" in texture:
                texture["sampler"] += bases["samplers"]
        for material in generated.get("materials", []):
            for fields in (material, material.get("pbrMetallicRoughness", {})):
                for key, value in fields.items():
                    if key.endswith("Texture"):
                        value["index"] += bases["textures"]
        active_scene = document["scenes"][document.get("scene", 0)]
        rotation = [1., 0., 0., 0., 0., 0., -1., 0., 0., 1., 0., 0., 0., 0., 0., 1.]
        for index, mesh in enumerate(generated["meshes"]):
            for primitive in mesh["primitives"]:
                primitive["attributes"] = {key: value + bases["accessors"]
                                           for key, value in primitive["attributes"].items()}
                primitive["indices"] += bases["accessors"]
                primitive["material"] += bases["materials"]
            active_scene.setdefault("nodes", []).append(len(document["nodes"]))
            document["nodes"].append({"name": mesh["name"], "mesh": index + bases["meshes"],
                                      "matrix": rotation})
        for key in bases:
            document[key].extend(generated.get(key, []))
        for extension in generated.get("extensionsUsed", []):
            if extension not in document.setdefault("extensionsUsed", []):
                document["extensionsUsed"].append(extension)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    metadata["fences"] = stats
    metadata.setdefault("sources", {})["fences"] = provenance
    document["buffers"][0]["byteLength"] = len(binary)
    return pack_glb(document, binary)


def update_fences(map_path: Path, config: dict, osm_path: Path, provenance: dict) -> dict:
    """Replace fence meshes, retaining the published terrain and town graph.

    Validate the candidate GLB and unchanged terrain before publication. Keep
    original map/report backups and roll back if companion publication fails.
    Tree and forest manifests remain separate and need refreshing afterwards.
    """
    started = time.monotonic()
    map_path = Path(map_path).resolve()
    terrain, frame, map_info = load_tree_map(map_path, config)
    original = map_path.read_bytes()
    payload = json.loads(Path(osm_path).read_text(encoding="utf-8"))
    features = parse_fences(payload, frame["transformer"], frame["origin"], frame["clip"])
    fence_scene = trimesh.Scene()
    stats = add_fences(fence_scene, features, terrain, {**config, "_clip_polygon": frame["clip"]})
    LOG.info("Appending fence buffers while preserving the published town binary")
    patched = prepare_fence_patch(original, fence_scene, stats, provenance)
    document, _ = read_glb(patched)
    metadata = document["scenes"][document.get("scene", 0)]["extras"]
    report_path = map_path.with_name("report.json")
    report_original = report_path.read_bytes() if report_path.exists() else None
    report = json.loads(report_original) if report_original is not None else dict(metadata)
    report.update(fences=stats, sources=metadata["sources"])
    pending = {}

    def stage(path, data):
        with tempfile.NamedTemporaryFile(prefix=".fences-", suffix=path.suffix,
                                         dir=path.parent, delete=False) as stream:
            stream.write(data)
            return Path(stream.name)

    try:
        pending[map_path] = stage(map_path, patched)
        LOG.info("Validating the complete candidate GLB and unchanged terrain")
        verification = inspect_glb(pending[map_path])
        updated_terrain, _, _ = load_tree_map(pending[map_path], config)
        if (not np.array_equal(terrain.heights, updated_terrain.heights)
                or not np.array_equal(terrain.x_axis, updated_terrain.x_axis)
                or not np.array_equal(terrain.y_axis, updated_terrain.y_axis)
                or terrain.datum != updated_terrain.datum):
            raise ValueError("Fence update changed the published terrain; refusing to publish")
        report.update(verification=verification, created_utc=datetime.now(timezone.utc).isoformat(),
                      elapsed_seconds=round(time.monotonic() - started, 2), update="fences_only")
        pending[report_path] = stage(report_path, (json.dumps(report, indent=2, ensure_ascii=False) + "\n").encode())
        if (map_path.read_bytes() != original
                or (report_path.read_bytes() if report_path.exists() else None) != report_original):
            raise ValueError("Map or report changed during fence preparation; retry when other work completes")
        backup = map_path.parent / "fence-backups" / map_info["map_sha256"]
        backup.mkdir(parents=True, exist_ok=True)
        for path, data in [(map_path, original), (report_path, report_original)]:
            if data is None:
                continue
            saved = backup / path.name
            if saved.exists() and saved.read_bytes() != data:
                raise ValueError("An existing fence backup differs; refusing to overwrite it")
            if not saved.exists():
                shutil.copy2(path, saved)
        published = []
        try:
            for path, temporary in pending.items():
                temporary.replace(path)
                published.append(path)
        except Exception:
            for path in reversed(published):
                data = original if path == map_path else report_original
                if data is None:
                    path.unlink(missing_ok=True)
                else:
                    stage(path, data).replace(path)
            raise
    finally:
        for temporary in pending.values():
            temporary.unlink(missing_ok=True)
    return report
