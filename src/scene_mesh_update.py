"""Replace named static scene layers while preserving original town buffers."""
from __future__ import annotations
from .glb import read_glb


def _remove_mesh_nodes(document: dict, prefixes: tuple[str, ...]) -> None:
    """Remove selected draws, remapping the static town's graph indices."""
    old_meshes = document["meshes"]
    retained = {i: mesh for i, mesh in enumerate(old_meshes)
                if not mesh.get("name", "").startswith(prefixes)}
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


def replace_scene_meshes(original, scene, prefixes, *, tree_postprocessor=None):
    """Return patched document/binary; retain unused original buffer bytes.

    Input layer vertices use local east/north/up metres. Appended leaf nodes
    rotate once into the runtime X-east/Y-up/Z-south frame.
    """
    document, binary = read_glb(original)
    if document.get("skins") or document.get("animations"):
        raise ValueError("Mesh updates require a static town without skins or animations")
    _remove_mesh_nodes(document, prefixes)
    if scene.geometry:
        generated, layer_binary = read_glb(scene.export(file_type="glb", tree_postprocessor=tree_postprocessor))
        binary += b"\0" * (-len(binary) % 4)
        offset = len(binary)
        binary += layer_binary
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
    return document, binary
