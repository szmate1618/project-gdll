#!/usr/bin/env python3
"""Merge a baked triangle navmesh into convex polygons without moving vertices.

This is an offline postprocessor, not a second rasterization or triangulation.
Coordinates are local X east / Z south in meters. NumPy and Shapely are required.
The version-2 format retains the original header provenance and vertex bytes.
"""

from __future__ import annotations

import argparse
import hashlib
import heapq
import json
from pathlib import Path
import resource
import struct
import tempfile
import time

import numpy as np
import shapely


HEADER = struct.Struct("<8sII8dQQ32s")
MAGIC_V1 = b"GDNAV01\0"
MAGIC_V2 = b"GDNAV02\0"
NO_POLYGON = 0xFFFFFFFF


def _edges(polygon):
    return zip(polygon, polygon[1:]+polygon[:1])


def _convex(vertices, polygon):
    """Use all edge half-planes; retain collinear vertices and reject zero area."""
    if len(set(polygon)) != len(polygon):
        return False
    p = vertices[polygon].astype(np.longdouble)
    edge = np.roll(p, -1, axis=0)-p
    if np.any(np.all(edge == 0, axis=1)):
        return False
    offset = p[None, :, :]-p[:, None, :]
    cross = edge[:, None, 0]*offset[:, :, 1]-edge[:, None, 1]*offset[:, :, 0]
    return bool(np.all(cross >= 0) and np.any(cross > 0))


def _topology(polygons):
    owners = {}
    for i, polygon in enumerate(polygons):
        for edge in _edges(polygon):
            if edge in owners:
                raise ValueError("duplicate directed edge or inconsistent winding")
            owners[edge] = i
    neighbors, boundary = [], set()
    for polygon in polygons:
        row = []
        for a, b in _edges(polygon):
            neighbor = owners.get((b, a), NO_POLYGON)
            row.append(neighbor)
            if neighbor == NO_POLYGON:
                boundary.add((a, b))
        neighbors.append(row)
    return neighbors, boundary, owners


def _components(neighbors):
    visited = bytearray(len(neighbors))
    count = 0
    for first in range(len(neighbors)):
        if visited[first]:
            continue
        count += 1
        stack = [first]
        visited[first] = 1
        while stack:
            for neighbor in neighbors[stack.pop()]:
                if neighbor != NO_POLYGON and not visited[neighbor]:
                    visited[neighbor] = 1
                    stack.append(neighbor)
    return count


def read_triangle_mesh(filename):
    """Read and independently check the v1 topology before considering merges."""
    data = Path(filename).read_bytes()
    if len(data) < HEADER.size:
        raise ValueError("truncated navmesh header")
    header = HEADER.unpack_from(data)
    magic, vertex_count, triangle_count = header[:3]
    bounds, padding = header[3:7], header[7:11]
    vertex_bytes, triangle_bytes = header[11:13]
    if magic != MAGIC_V1:
        raise ValueError("convex merging requires a GDNAV01 triangle mesh")
    if not (0 < vertex_count < NO_POLYGON and 0 < triangle_count < NO_POLYGON):
        raise ValueError("navmesh requires nonempty 32-bit vertex and triangle counts")
    if (vertex_bytes != 16*vertex_count or triangle_bytes != 24*triangle_count or
            len(data) != HEADER.size+vertex_bytes+triangle_bytes):
        raise ValueError("navmesh payload sizes do not match its header")
    if (not np.all(np.isfinite(bounds+padding)) or
            bounds[0] >= bounds[2] or bounds[1] >= bounds[3] or min(padding) < 0):
        raise ValueError("invalid navmesh bounds or padding")
    vertices = np.frombuffer(data, dtype="<f8", count=vertex_count*2,
                             offset=HEADER.size).reshape(-1, 2)
    records = np.frombuffer(data, dtype="<u4", offset=HEADER.size+vertex_bytes).reshape(-1, 6)
    triangles = records[:, :3]
    if (not np.all(np.isfinite(vertices)) or np.any(triangles >= vertex_count) or
            np.any(vertices < bounds[:2]) or np.any(vertices > bounds[2:])):
        raise ValueError("invalid navmesh vertex coordinates or triangle indices")
    polygons = triangles.tolist()
    for polygon in polygons:
        if not _convex(vertices, polygon):
            raise ValueError("input triangle must be nondegenerate and counterclockwise")
    neighbors, boundary, _ = _topology(polygons)
    if not np.array_equal(np.asarray(neighbors, dtype=np.uint32), records[:, 3:]):
        raise ValueError("input neighbors do not match shared directed edges")
    return data, header, vertices, polygons, neighbors, boundary


def _joined_polygon(a, b, shared_start, shared_end):
    """Remove one reciprocal edge, retaining every other boundary vertex."""
    ai = a.index(shared_start)
    bi = b.index(shared_end)
    if a[(ai+1) % len(a)] != shared_end or b[(bi+1) % len(b)] != shared_start:
        raise ValueError("merge candidate has no reciprocal shared edge")
    left = a[ai+1:]+a[:ai+1]
    right = b[bi+1:]+b[:bi+1]
    return left+right[1:-1]


def merge_polygons(vertices, triangles, max_vertices=4):
    """Deterministically prefer long removable edges, updating only local edges.

    Heap entries include polygon versions so no stale candidate can merge a
    changed boundary. Multiple shared edges and repeated vertices are rejected;
    consequently a merge cannot remove a hole or an interior vertex.
    """
    if not 3 <= max_vertices <= 6:
        raise ValueError("max_vertices must be between 3 and 6")
    polygons = [list(polygon) for polygon in triangles]
    _, _, owners = _topology(polygons)
    memberships = [[i] for i in range(len(polygons))]
    versions = [0]*len(polygons)
    heap = []

    def enqueue(a, b, start, end):
        if a > b:
            a, b, start, end = b, a, end, start
        if len(polygons[a])+len(polygons[b])-2 > max_vertices:
            return
        delta = vertices[start].astype(np.longdouble)-vertices[end]
        length_squared = delta[0]*delta[0]+delta[1]*delta[1]
        heapq.heappush(heap, (-length_squared, a, b, versions[a], versions[b], start, end))

    for (start, end), a in owners.items():
        b = owners.get((end, start))
        if b is not None and a < b:
            enqueue(a, b, start, end)
    merges = 0
    while heap:
        _, a, b, va, vb, start, end = heapq.heappop(heap)
        if not polygons[a] or not polygons[b] or versions[a] != va or versions[b] != vb:
            continue
        shared = sum(owners.get((v, u)) == b for u, v in _edges(polygons[a]))
        if shared != 1:
            continue
        joined = _joined_polygon(polygons[a], polygons[b], start, end)
        if not _convex(vertices, joined):
            continue
        for polygon in (polygons[a], polygons[b]):
            for edge in _edges(polygon):
                del owners[edge]
        polygons[a], polygons[b] = joined, []
        memberships[a].extend(memberships[b])
        memberships[b] = []
        versions[a] += 1
        versions[b] += 1
        merges += 1
        for edge in _edges(joined):
            owners[edge] = a
        for u, v in _edges(joined):
            other = owners.get((v, u))
            if other is not None:
                enqueue(a, other, u, v)
    alive = [i for i, polygon in enumerate(polygons) if polygon]
    mapping = np.full(len(triangles), NO_POLYGON, dtype=np.uint32)
    for output_id, i in enumerate(alive):
        mapping[memberships[i]] = output_id
    return [polygons[i] for i in alive], mapping, merges


def validate_merge(vertices, triangles, source_neighbors, source_boundary, polygons, mapping):
    """Check unchanged topology and GEOS coverage independently of merge tests."""
    if len(mapping) != len(triangles) or np.any(mapping >= len(polygons)):
        raise ValueError("triangle membership must cover the complete input exactly once")
    if not np.array_equal(np.unique(mapping), np.arange(len(polygons))):
        raise ValueError("merged polygon has no source triangle membership")
    if any(not 3 <= len(p) <= 6 or not _convex(vertices, p) for p in polygons):
        raise ValueError("merged polygon is not convex and counterclockwise")
    neighbors, boundary, _ = _topology(polygons)
    if boundary != source_boundary:
        raise ValueError("merging changed an obstacle or map boundary edge")
    components = _components(neighbors)
    if components != _components(source_neighbors):
        raise ValueError("merging changed connected components")
    # GEOS predicates are independent of the long-double half-plane merge test.
    source_faces = shapely.polygons(vertices[np.asarray(triangles, dtype=np.uint32)])
    output_faces = np.empty(len(polygons), dtype=object)
    sizes = np.asarray([len(polygon) for polygon in polygons])
    for count in range(3, 7):
        selected = np.flatnonzero(sizes == count)
        if len(selected):
            ids = np.asarray([polygons[i] for i in selected], dtype=np.uint32)
            output_faces[selected] = shapely.polygons(vertices[ids])
    if not np.all(shapely.is_valid(output_faces)):
        raise ValueError("GEOS rejected a merged polygon")
    if not np.all(shapely.covers(output_faces[mapping], source_faces)):
        raise ValueError("merged polygon does not cover an assigned input triangle")
    source_area = float(np.sum(shapely.area(source_faces)))
    output_area = float(np.sum(shapely.area(output_faces)))
    if abs(source_area-output_area) > max(1e-8, source_area*1e-12):
        raise ValueError("merging changed free-space area")
    return neighbors, {"components": components, "boundary_edges": len(boundary),
                       "source_area_m2": source_area, "polygon_area_m2": output_area,
                       "geos_covered_triangles": len(triangles),
                       "boundary_edges_identical": True,
                       "triangle_membership_complete": True}


def _binary_payload(source, header, polygons, neighbors):
    polygon_bytes = sum(4+8*len(polygon) for polygon in polygons)
    result = bytearray(HEADER.size+header[11]+polygon_bytes)
    values = list(header)
    values[0], values[2], values[12] = MAGIC_V2, len(polygons), polygon_bytes
    HEADER.pack_into(result, 0, *values)
    result[HEADER.size:HEADER.size+header[11]] = source[HEADER.size:HEADER.size+header[11]]
    offset = HEADER.size+header[11]
    for polygon, adjacent in zip(polygons, neighbors):
        struct.pack_into("<I", result, offset, len(polygon))
        offset += 4
        for row in (polygon, adjacent):
            payload = np.asarray(row, dtype="<u4").tobytes()
            result[offset:offset+len(payload)] = payload
            offset += len(payload)
    if offset != len(result):
        raise ValueError("serialized polygon payload has an unexpected size")
    return result


def _atomic_write(filename, data):
    filename = Path(filename)
    filename.parent.mkdir(parents=True, exist_ok=True)
    temporary = None
    try:
        with tempfile.NamedTemporaryFile(dir=filename.parent, prefix=filename.name+".",
                                         suffix=".tmp", delete=False) as output:
            temporary = Path(output.name)
            output.write(data)
        temporary.replace(filename)
    finally:
        if temporary is not None and temporary.exists():
            temporary.unlink()


def merge_file(input_filename, output_filename, max_vertices=4):
    """Validate everything before atomically replacing a previously good mesh."""
    if Path(input_filename).resolve() == Path(output_filename).resolve():
        raise ValueError("output must differ from the source triangle mesh")
    started = time.perf_counter()
    source, header, vertices, triangles, source_neighbors, boundary = read_triangle_mesh(input_filename)
    read_seconds = time.perf_counter()-started
    merging_started = time.perf_counter()
    polygons, mapping, merges = merge_polygons(vertices, triangles, max_vertices)
    merge_seconds = time.perf_counter()-merging_started
    validation_started = time.perf_counter()
    neighbors, validation = validate_merge(vertices, triangles, source_neighbors, boundary,
                                            polygons, mapping)
    validation_seconds = time.perf_counter()-validation_started
    payload = _binary_payload(source, header, polygons, neighbors)
    histogram = {str(count): sum(len(p) == count for p in polygons) for count in range(3, 7)}
    metadata = {
        "format": "GDNAV02", "input": str(input_filename),
        "source_file_sha256": hashlib.sha256(source).hexdigest(),
        "source_replay_sha256": header[-1].hex(), "vertices": len(vertices),
        "source_triangles": len(triangles), "polygons": len(polygons), "merges": merges,
        "max_vertices": max_vertices, "vertex_count_histogram": histogram,
        "directed_edges": sum(len(p) for p in polygons),
        "bounds": header[3:7], "radius_m": header[7], "clearance_m": header[8],
        "building_extra_m": header[9], "numeric_margin_m": header[10],
        "vertex_payload_bytes": header[11], "polygon_payload_bytes": len(payload)-128-header[11],
        "file_bytes": len(payload), "output_sha256": hashlib.sha256(payload).hexdigest(),
        "read_seconds": read_seconds, "merge_seconds": merge_seconds,
        "validation_seconds": validation_seconds, "total_seconds": time.perf_counter()-started,
        "peak_rss_bytes": resource.getrusage(resource.RUSAGE_SELF).ru_maxrss*1024,
        "shapely_version": shapely.__version__, "geos_version": shapely.geos_version_string,
        "validation": validation,
        # This gives external validators an explicit, complete correspondence.
        "triangle_to_polygon": mapping.tolist(),
    }
    sidecar = Path(output_filename).with_name(Path(output_filename).name+".json")
    sidecar_data = (json.dumps(metadata, indent=2)+"\n").encode()
    _atomic_write(sidecar, sidecar_data)
    _atomic_write(output_filename, payload)
    return metadata


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--max-vertices", type=int, choices=(3, 4, 6), default=4,
                        help="maximum vertices per merged polygon (default: 4)")
    args = parser.parse_args()
    metadata = merge_file(args.input, args.output, args.max_vertices)
    print(json.dumps({k: v for k, v in metadata.items() if k != "triangle_to_polygon"}, indent=2))


if __name__ == "__main__":
    main()
