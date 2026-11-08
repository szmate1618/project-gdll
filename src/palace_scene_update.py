"""Apply the palace override to an existing town without regenerating its area."""
from __future__ import annotations

from pathlib import Path

import numpy as np
from shapely.geometry import Polygon
import trimesh

from .buildings import add_buildings
from .config import coordinate_frame
from .glb import read_glb, pack_glb
from .palace_floorplan import definition
from .palace_roof import roof_definition
from .scene_mesh_update import replace_scene_meshes


def prepare_palace_patch(original, terrain, config):
    """Replace only this palace's meshes, using the published terrain datum."""
    data = definition()
    name = "building_" + data["osm_id"].replace("/", "_") + "_0"
    document, _ = read_glb(original)
    old_wall = next((m for m in document["meshes"] if m.get("name") == name + "_walls"), None)
    if old_wall is None:
        raise ValueError("The selected map does not contain the mapped Gödöllő palace")
    frame = coordinate_frame(config)
    points = np.array([frame["transformer"].transform(*p) for p in data["reference_footprint_lonlat"]])
    points -= frame["origin"]
    polygon = Polygon(points)
    # Older and already-patched town walls use the same source ENU metre frame.
    # Refuse a changed footprint rather than applying a stale geographic trace.
    positions = document["accessors"][old_wall["primitives"][0]["attributes"]["POSITION"]]
    if not (np.allclose(positions["min"][:2], points.min(axis=0), atol=.02, rtol=0) and
            np.allclose(positions["max"][:2], points.max(axis=0), atol=.02, rtol=0)):
        raise ValueError("Existing palace footprint differs from the reference outline")
    scene = trimesh.Scene()
    stats = add_buildings(scene, [{"id": data["osm_id"], "geometry": polygon,
                                  "tags": {"building": "yes", "building:levels": "2"}}], terrain, config)
    if stats["buildings"] != 1:
        raise ValueError("Palace geometry preparation failed")
    updated, binary = replace_scene_meshes(original, scene, (name + "_",))
    metadata = updated["scenes"][updated.get("scene", 0)].setdefault("extras", {})
    metadata["palace_interior"] = {"version": 1, "source": data["sources"],
                                   "storeys": 2, "exterior_doors": stats["doors"], "windows": stats["windows"],
                                   "roof": {"version": 1, "source": roof_definition()["sources"],
                                            "hips": len(roof_definition()["hips"]),
                                            "cupolas": len(roof_definition()["cupolas"])}}
    updated["buffers"][0]["byteLength"] = len(binary)
    return pack_glb(updated, binary), metadata["palace_interior"]


def update_palace(map_path: Path, config: dict, companions=()):
    """Publish the palace override with validated backups and matching companions."""
    from .authored_scene_update import update_authored_building
    return update_authored_building(map_path, config, companions, prepare_palace_patch, "palace")
