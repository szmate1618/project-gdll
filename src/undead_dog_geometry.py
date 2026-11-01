"""Flatten the cached dog's tongue while preserving its top and jaw attachment."""
from __future__ import annotations

import copy

import numpy as np

from .glb import pack_glb, read_glb
from .zombie_lod import accessor_array, numeric_attribute


def _vec3_view(document, binary, index):
    accessor = document["accessors"][index]
    if accessor["componentType"] != 5126 or accessor["type"] != "VEC3" or accessor.get("normalized"):
        raise ValueError("Tongue geometry requires float32 VEC3 attributes")
    accessor_array(document, binary, index)  # Validate dense accessor bounds.
    view = document["bufferViews"][accessor["bufferView"]]
    offset = view.get("byteOffset", 0) + accessor.get("byteOffset", 0)
    return np.ndarray((accessor["count"], 3), dtype="<f4", buffer=binary,
                      offset=offset, strides=(view.get("byteStride", 12), 4))


def flatten_tongue(source: bytes) -> tuple[bytes, dict]:
    """Halve vertical thickness below the sloping top plane, in model meters.

    The source tongue is a closed pentagonal prism, rigidly attached to the jaw.
    Its UVs occupy the second tile of the bottom atlas row. All seam copies move
    together; X/Z, top vertices, topology and all skin/animation bytes stay fixed.
    Always call with the original cached GLB so rebuilds cannot flatten it again.
    """
    document, source_binary = read_glb(source)
    result = copy.deepcopy(document)
    if len(document["meshes"]) != 1 or len(document["meshes"][0]["primitives"]) != 1:
        raise ValueError("Expected the original single-mesh undead dog")
    primitive = document["meshes"][0]["primitives"][0]
    attrs = primitive["attributes"]
    positions = numeric_attribute(document, source_binary, attrs["POSITION"])
    normals = numeric_attribute(document, source_binary, attrs["NORMAL"])
    uv = numeric_attribute(document, source_binary, attrs["TEXCOORD_0"])
    faces = accessor_array(document, source_binary, primitive["indices"]).reshape(-1, 3)
    joints = accessor_array(document, source_binary, attrs["JOINTS_0"])
    weights = numeric_attribute(document, source_binary, attrs["WEIGHTS_0"])
    mesh_node = next(n for n in document["nodes"] if n.get("mesh") == 0)
    skin = document["skins"][mesh_node["skin"]]
    jaw = next(i for i, n in enumerate(skin["joints"]) if document["nodes"][n].get("name") == "jaw")
    selected = (uv[:, 0] > .25) & (uv[:, 0] < .5) & (uv[:, 1] > .75) & (uv[:, 1] < 1)
    tongue_faces = faces[np.all(selected[faces], axis=1)]
    if len(tongue_faces) != 16 or np.any(np.any(selected[faces], axis=1) != np.all(selected[faces], axis=1)):
        raise ValueError("Expected the complete 16-triangle tongue UV island")
    if not np.all((joints[selected, 0] == jaw) & (weights[selected, 0] == 1)):
        raise ValueError("Tongue vertices must be rigidly attached to the jaw")
    top_faces = tongue_faces[np.all(normals[tongue_faces, 1] > .9, axis=1)]
    if len(top_faces) != 2:
        raise ValueError("Expected two triangles on the tongue's sloping top")
    a, b, c = positions[top_faces[0]].astype(np.float64)
    normal = np.cross(b-a, c-a)
    normal /= np.linalg.norm(normal)
    top = positions[np.unique(top_faces)].astype(np.float64)
    if normal[1] < .9 or not np.allclose((top-a) @ normal, 0, atol=1e-6):
        raise ValueError("Tongue top must be a consistent upward plane")
    tongue = positions[selected].astype(np.float64)
    slope = -normal[[0, 2]] / normal[1]
    top_y = a[1] + (tongue[:, [0, 2]] - a[[0, 2]]) @ slope
    depth = top_y - tongue[:, 1]
    if np.any(depth < -1e-6):
        raise ValueError("Tongue geometry extends above its top surface")
    lower = depth > 1e-6
    ids = np.flatnonzero(selected)
    binary = bytearray(source_binary)
    position_view = _vec3_view(result, binary, attrs["POSITION"])
    position_view[ids[lower], 1] = tongue[lower, 1] + depth[lower] * .5
    # Inverse-transpose normals for the affine change in Y below the top plane.
    linear = np.eye(3)
    linear[1] = [slope[0] * .5, .5, slope[1] * .5]
    updated_normals = normals[selected].astype(np.float64) @ np.linalg.inv(linear)
    updated_normals /= np.linalg.norm(updated_normals, axis=1, keepdims=True)
    _vec3_view(result, binary, attrs["NORMAL"])[selected] = updated_normals
    spec = result["accessors"][attrs["POSITION"]]
    spec["min"] = position_view.min(axis=0).tolist()
    spec["max"] = position_view.max(axis=0).tolist()
    return pack_glb(result, bytes(binary)), {
        "preparation": "src/undead_dog_geometry.py",
        "tongue_thickness_scale": .5,
        "lower_vertices_raised": int(lower.sum()),
        "top_vertices_fixed": int((~lower).sum()),
        "frame": "model Y up, meters; top plane fixed, X/Z unchanged",
    }
