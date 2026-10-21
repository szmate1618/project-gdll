#!/usr/bin/env python3
"""Bake a planar, clearance-aware triangle navmesh from a path benchmark scene.

Requires Shapely >= 2.1 (GEOS >= 3.10) and NumPy. Runtime navigation does not
depend on Python. Coordinates are local X east / Z south, in meters.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import resource
import struct
import time

import numpy as np
import shapely


NO_TRIANGLE = np.uint32(0xFFFFFFFF)
HEADER = struct.Struct("<8sII8dQQ32s")
MAGIC = b"GDNAV01\0"
SEAM_TOLERANCE = 1e-7


def obstacle_polygons(obstacles, radius=.3, clearance=.002,
                      building_extra=.05, numeric_margin=.005):
    """Conservatively inflate each oriented rectangle, including every trunk."""
    a = np.asarray(obstacles, dtype=np.float64)
    if a.size == 0:
        return np.empty(0, dtype=object)
    if a.ndim != 2 or a.shape[1] != 7 or not np.all(np.isfinite(a)):
        raise ValueError("obstacles must be finite [kind,cx,cz,ux,uz,hx,hz] records")
    if np.any(a[:, 5:7] < 0) or np.any(~np.isin(a[:, 0], [0, 1, 2])):
        raise ValueError("invalid obstacle kind or half extent")
    u = a[:, 3:5].copy()
    norm = np.linalg.norm(u, axis=1)
    if np.any(norm < 1e-12):
        raise ValueError("obstacle axes must be nonzero")
    u /= norm[:, None]
    v = np.stack((-u[:, 1], u[:, 0]), axis=1)
    padding = radius + clearance + numeric_margin + (a[:, 0] == 0) * building_extra
    h = a[:, 5:7] + padding[:, None]
    c = a[:, 1:3]
    du, dv = u * h[:, 0, None], v * h[:, 1, None]
    return shapely.polygons(np.stack((c-du-dv, c+du-dv, c+du+dv, c-du+dv), axis=1))


def _ccw(coordinates):
    coordinates = np.asarray(coordinates, dtype=np.float64).copy()
    area = ((coordinates[:, 1, 0]-coordinates[:, 0, 0]) *
            (coordinates[:, 2, 1]-coordinates[:, 0, 1]) -
            (coordinates[:, 1, 1]-coordinates[:, 0, 1]) *
            (coordinates[:, 2, 0]-coordinates[:, 0, 0]))
    if np.any(area == 0):
        raise ValueError("degenerate triangle produced by constrained triangulation")
    swap = area < 0
    coordinates[swap] = coordinates[swap][:, [0, 2, 1], :]
    return coordinates


def _boundaries(low, high, tile_size):
    count = math.ceil((high-low) / tile_size)
    return np.array([low + i*tile_size for i in range(count)] + [high], dtype=np.float64)


def triangulate_tiles(polygons, bounds, tile_size=256., progress=None):
    """Use genuine constrained Delaunay triangles on polygonal free space.

    Small independent bake tiles avoid GEOS's expensive triangulation of one
    polygon with over a hundred thousand holes. Tiles are only a bake partition;
    they impose no restrictions on runtime routes or the funnel corridor.
    """
    if not np.isfinite(tile_size) or tile_size <= 0:
        raise ValueError("tile size must be positive")
    x0, z0, x1, z1 = bounds
    xs, zs = _boundaries(x0, x1, tile_size), _boundaries(z0, z1, tile_size)
    tree = shapely.STRtree(polygons)
    all_triangles = []
    area = 0.
    count = 0
    union_seconds = cdt_seconds = 0.
    total_tiles = (len(xs)-1) * (len(zs)-1)
    for iz in range(len(zs)-1):
        for ix in range(len(xs)-1):
            tile = shapely.box(xs[ix], zs[iz], xs[ix+1], zs[iz+1])
            candidates = tree.query(tile)
            started = time.perf_counter()
            blocked = shapely.union_all(polygons[candidates])
            free = shapely.difference(tile, blocked)
            union_seconds += time.perf_counter()-started
            started = time.perf_counter()
            triangles = shapely.constrained_delaunay_triangles(free)
            cdt_seconds += time.perf_counter()-started
            parts = shapely.get_parts(triangles)
            if len(parts):
                coordinates = shapely.get_coordinates(parts).reshape(-1, 4, 2)[:, :3, :]
                all_triangles.append(_ccw(coordinates))
                count += len(parts)
            area += float(shapely.area(free))
            if progress and (ix == len(xs)-2 or (iz == 0 and ix == 0)):
                progress((iz*(len(xs)-1)+ix+1), total_tiles, count)
    coordinates = (np.concatenate(all_triangles) if all_triangles else
                   np.empty((0, 3, 2), dtype=np.float64))
    # Include the outer map lines in canonicalization as well: otherwise a
    # corner shared with an internal seam could round off the exact map edge.
    return coordinates, xs, zs, {
        "tile_size_m": tile_size, "tiles": total_tiles, "free_area_m2": area,
        "tile_triangles": count, "union_seconds": union_seconds,
        "triangulation_seconds": cdt_seconds,
    }


def _canonicalize_seams(coordinates, x_lines, z_lines):
    """Only round coordinates on tile seams; retain full precision elsewhere."""
    result = coordinates.copy()
    flat = result.reshape(-1, 2)
    for axis, lines in ((0, x_lines), (1, z_lines)):
        if not len(lines):
            continue
        indices = np.searchsorted(lines, flat[:, axis])
        for offset in (0, -1):
            j = np.clip(indices+offset, 0, len(lines)-1)
            selected = np.abs(flat[:, axis]-lines[j]) <= SEAM_TOLERANCE
            flat[selected, axis] = lines[j[selected]]
            flat[selected, 1-axis] = np.round(flat[selected, 1-axis], 7)
    # A tile corner belongs to both axes; snapping its second axis must not
    # leave the first axis rounded away from the exact shared tile coordinate.
    for axis, lines in ((0, x_lines), (1, z_lines)):
        if len(lines):
            indices = np.searchsorted(lines, flat[:, axis])
            for offset in (0, -1):
                j = np.clip(indices+offset, 0, len(lines)-1)
                selected = np.abs(flat[:, axis]-lines[j]) <= SEAM_TOLERANCE
                flat[selected, axis] = lines[j[selected]]
    return result


def conform_tile_seams(coordinates, x_lines, z_lines):
    """Split T junctions so each triangle has at most three true neighbors.

    Adjacent tile triangulations can subdivide their shared free interval
    differently. Collect both sets of boundary vertices, split every affected
    edge, and triangulate the affected triangle with an interior centroid fan.
    This preserves the free polygon rather than connecting nearby vertices
    across an obstacle or relying on an assumed matching subdivision.
    """
    coordinates = _canonicalize_seams(coordinates, x_lines, z_lines)
    vertices, inverse = np.unique(coordinates.reshape(-1, 2), axis=0, return_inverse=True)
    triangles = inverse.reshape(-1, 3).astype(np.uint32)
    line_vertices = {}
    for axis, lines in ((0, x_lines), (1, z_lines)):
        for line in lines:
            ids = np.flatnonzero(vertices[:, axis] == line)
            if len(ids):
                order = np.argsort(vertices[ids, 1-axis])
                line_vertices[axis, float(line)] = (vertices[ids[order], 1-axis], ids[order])
    additions = []
    centroids = []
    keep = np.ones(len(triangles), dtype=bool)
    split_count = 0
    for ti, tri in enumerate(triangles):
        polygon = []
        split = False
        for edge in range(3):
            a, b = int(tri[edge]), int(tri[(edge+1) % 3])
            polygon.append(a)
            for axis in (0, 1):
                if vertices[a, axis] != vertices[b, axis]:
                    continue
                line = line_vertices.get((axis, float(vertices[a, axis])))
                if line is None:
                    continue
                values, ids = line
                av, bv = vertices[a, 1-axis], vertices[b, 1-axis]
                lo = np.searchsorted(values, min(av, bv), side="right")
                hi = np.searchsorted(values, max(av, bv), side="left")
                if hi > lo:
                    mids = ids[lo:hi]
                    polygon.extend(mids if av < bv else mids[::-1])
                    split = True
                break
        if split:
            keep[ti] = False
            ci = len(vertices)+len(centroids)
            centroids.append(np.mean(vertices[tri], axis=0))
            additions.extend((ci, polygon[i], polygon[(i+1) % len(polygon)])
                             for i in range(len(polygon)))
            split_count += 1
    if centroids:
        vertices = np.concatenate((vertices, np.asarray(centroids)))
        triangles = np.concatenate((triangles[keep], np.asarray(additions, dtype=np.uint32)))
    # A shared endpoint may differ at the last rounding bit after canonicalization.
    # Zero-area faces are rejected rather than silently opening or losing a hole.
    checked = _ccw(vertices[triangles])
    negative = np.any(checked != vertices[triangles], axis=(1, 2))
    triangles[negative] = triangles[negative][:, [0, 2, 1]]
    return vertices, triangles, {"seam_split_triangles": split_count}


def build_neighbors(triangles):
    neighbors = np.full((len(triangles), 3), NO_TRIANGLE, dtype=np.uint32)
    if not len(triangles):
        return neighbors
    starts, ends = triangles.reshape(-1), np.roll(triangles, -1, axis=1).reshape(-1)
    low, high = np.minimum(starts, ends), np.maximum(starts, ends)
    order = np.lexsort((high, low))
    same = ((low[order[:-1]] == low[order[1:]]) &
            (high[order[:-1]] == high[order[1:]]))
    if np.any(same[:-1] & same[1:]):
        raise ValueError("nonmanifold edge has more than two incident triangles")
    pairs = np.flatnonzero(same)
    a, b = order[pairs], order[pairs+1]
    if np.any(starts[a] != ends[b]) or np.any(ends[a] != starts[b]):
        raise ValueError("neighboring triangles have inconsistent winding")
    neighbors.reshape(-1)[a] = b//3
    neighbors.reshape(-1)[b] = a//3
    return neighbors


def validate_boundaries(vertices, triangles, neighbors, polygons, bounds):
    """Every open edge must follow a real obstacle or the outer map boundary."""
    face, edge = np.where(neighbors == NO_TRIANGLE)
    a, b = vertices[triangles[face, edge]], vertices[triangles[face, (edge+1) % 3]]
    x0, z0, x1, z1 = bounds
    outside = np.zeros(len(a), dtype=bool)
    for axis, value in ((0, x0), (0, x1), (1, z0), (1, z1)):
        outside |= (np.abs(a[:, axis]-value) < 1e-8) & (np.abs(b[:, axis]-value) < 1e-8)
    a, b = a[~outside], b[~outside]
    tree = shapely.STRtree(polygons)
    # Three samples also catch an unmatched seam spanning a blocked interval.
    for fraction in (.25, .5, .75):
        samples = shapely.points(a+(b-a)*fraction)
        hits = tree.query(samples, predicate="dwithin", distance=SEAM_TOLERANCE*3)
        covered = np.zeros(len(samples), dtype=bool)
        if hits.size:
            covered[hits[0]] = True
        if not np.all(covered):
            i = int(np.flatnonzero(~covered)[0])
            raise ValueError(f"unmatched free-space edge (tile crack): {a[i]} -> {b[i]}")
    return int(len(face))


def count_components(neighbors):
    visited = bytearray(len(neighbors))
    count = 0
    for start in range(len(neighbors)):
        if visited[start]:
            continue
        count += 1
        visited[start] = 1
        stack = [start]
        while stack:
            current = stack.pop()
            for n in neighbors[current]:
                if n != NO_TRIANGLE and not visited[int(n)]:
                    visited[int(n)] = 1
                    stack.append(int(n))
    return count


def write_mesh(filename, vertices, triangles, neighbors, bounds, source_sha256,
               radius=.3, clearance=.002, building_extra=.05, numeric_margin=.005):
    """Atomically publish version-1 little-endian runtime data."""
    if not len(vertices) or not len(triangles):
        raise ValueError("navmesh requires nonempty vertices and walkable triangles")
    if len(vertices) >= 0xFFFFFFFF or len(triangles) >= 0xFFFFFFFF:
        raise ValueError("mesh exceeds 32-bit indexing")
    vertices = np.asarray(vertices, dtype="<f8")
    records = np.concatenate((triangles, neighbors), axis=1).astype("<u4", copy=False)
    header = HEADER.pack(MAGIC, len(vertices), len(triangles), *bounds,
                         radius, clearance, building_extra, numeric_margin,
                         vertices.nbytes, records.nbytes, bytes.fromhex(source_sha256))
    filename = Path(filename)
    filename.parent.mkdir(parents=True, exist_ok=True)
    temporary = filename.with_name(filename.name+".tmp")
    with temporary.open("wb") as output:
        output.write(header)
        output.write(vertices.tobytes())
        output.write(records.tobytes())
    temporary.replace(filename)


def bake(scene, tile_size=256., radius=.3, clearance=.002,
         building_extra=.05, numeric_margin=.005, progress=None):
    minimum, maximum = scene["raster"]["minimum"], scene["raster"]["maximum"]
    bounds = (*minimum, *maximum)
    if len(bounds) != 4 or not np.all(np.isfinite(bounds)) or not (bounds[0] < bounds[2] and bounds[1] < bounds[3]):
        raise ValueError("invalid map bounds")
    if any(not np.isfinite(v) or v < 0 for v in (radius, clearance, building_extra, numeric_margin)):
        raise ValueError("padding must be finite and nonnegative")
    started = time.perf_counter()
    polygons = obstacle_polygons(scene["obstacles"], radius, clearance, building_extra, numeric_margin)
    coordinates, x_lines, z_lines, metadata = triangulate_tiles(polygons, bounds, tile_size, progress)
    if not len(coordinates):
        raise ValueError("navmesh has no walkable triangles")
    metadata["tile_bake_seconds"] = time.perf_counter()-started
    started = time.perf_counter()
    vertices, triangles, seam_metadata = conform_tile_seams(coordinates, x_lines, z_lines)
    neighbors = build_neighbors(triangles)
    metadata.update(seam_metadata)
    metadata["topology_seconds"] = time.perf_counter()-started
    started = time.perf_counter()
    metadata["boundary_edges"] = validate_boundaries(vertices, triangles, neighbors, polygons, bounds)
    metadata["components"] = count_components(neighbors)
    coordinates = vertices[triangles]
    ab, ac = coordinates[:, 1]-coordinates[:, 0], coordinates[:, 2]-coordinates[:, 0]
    area = float(np.sum(ab[:, 0]*ac[:, 1]-ab[:, 1]*ac[:, 0])*.5)
    if abs(area-metadata["free_area_m2"]) > max(1e-5, metadata["free_area_m2"]*1e-8):
        raise ValueError("triangle coverage differs from polygonal free-space area")
    metadata["validation_seconds"] = time.perf_counter()-started
    metadata.update({"vertices": len(vertices), "triangles": len(triangles),
                     "mesh_payload_bytes": int(vertices.nbytes+triangles.nbytes+neighbors.nbytes),
                     "triangle_area_m2": area, "bounds": bounds,
                     "obstacles": len(polygons), "radius_m": radius,
                     "clearance_m": clearance, "building_extra_m": building_extra,
                     "numeric_margin_m": numeric_margin, "shapely_version": shapely.__version__,
                     "geos_version": shapely.geos_version_string,
                     "peak_rss_bytes": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024})
    return vertices, triangles, neighbors, bounds, metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--scene", "--replay", required=True, type=Path)
    parser.add_argument("--output", required=True, type=Path)
    parser.add_argument("--tile-size", type=float, default=256.)
    parser.add_argument("--radius", type=float, default=.3)
    parser.add_argument("--clearance", type=float, default=.002)
    parser.add_argument("--building-extra", type=float, default=.05)
    parser.add_argument("--numeric-margin", type=float, default=.005)
    args = parser.parse_args()
    source = args.scene.read_bytes()
    started = time.perf_counter()
    result = bake(json.loads(source), args.tile_size, args.radius, args.clearance,
                  args.building_extra, args.numeric_margin,
                  lambda done, total, triangles: print(f"tiles {done}/{total}; triangles {triangles}", flush=True))
    vertices, triangles, neighbors, bounds, metadata = result
    sha256 = hashlib.sha256(source).hexdigest()
    write_mesh(args.output, vertices, triangles, neighbors, bounds, sha256,
               args.radius, args.clearance, args.building_extra, args.numeric_margin)
    metadata.update({"source": str(args.scene), "source_sha256": sha256,
                     "model": json.loads(source).get("model"),
                     "total_seconds": time.perf_counter()-started,
                     "peak_rss_bytes": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024,
                     "file_bytes": args.output.stat().st_size})
    sidecar = args.output.with_name(args.output.name+".json")
    temporary = sidecar.with_name(sidecar.name+".tmp")
    temporary.write_text(json.dumps(metadata, indent=2)+"\n")
    temporary.replace(sidecar)
    print(json.dumps(metadata, indent=2))


if __name__ == "__main__":
    main()
