#!/usr/bin/env python3
"""Compare independent navmesh search priorities on formation and random pairs.

Run from the repository root with its geometry-enabled Python environment.
The output directory must not contain a previous candidate executable. Query
timing comes from the C++ benchmark; this harness and GEOS checks are untimed.
"""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics
import struct
import subprocess

import numpy as np
import shapely


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def endpoint_digest(routes):
    return hashlib.sha256(b"".join(struct.pack("<4d", *r["start"], *r["goal"])
                                  for r in routes)).hexdigest()


def physical_obstacles(scene, mesh_metadata):
    boxes = np.asarray(scene["obstacles"], dtype=np.float64)
    assert not np.any(boxes[:, 0] == 1), "This comparison requires the tree-free fixture"
    u = boxes[:, 3:5].copy()
    u /= np.linalg.norm(u, axis=1)[:, None]
    v = np.stack((-u[:, 1], u[:, 0]), axis=1)
    padding = (mesh_metadata["radius_m"] + mesh_metadata["clearance_m"] +
               (boxes[:, 0] == 0) * mesh_metadata["building_extra_m"])
    half = boxes[:, 5:7] + padding[:, None]
    c, du, dv = boxes[:, 1:3], u * half[:, 0, None], v * half[:, 1, None]
    return shapely.STRtree(shapely.polygons(np.stack((c-du-dv, c+du-dv, c+du+dv, c-du+dv), axis=1)))


def component_index(mesh_path):
    """Independent offline connectivity classification; never used by searches."""
    data = Path(mesh_path).read_bytes()
    magic, vertex_count, count = struct.unpack_from("<8sII", data)
    assert magic == b"GDNAV02\0"
    vertex_bytes, polygon_bytes = struct.unpack_from("<QQ", data, 80)
    assert vertex_bytes == vertex_count * 16
    vertices = np.frombuffer(data, dtype="<f8", count=vertex_bytes//8, offset=128).reshape((-1, 2))
    offset = 128 + vertex_bytes
    polygons, neighbors = [], []
    for _ in range(count):
        size, = struct.unpack_from("<I", data, offset)
        offset += 4
        ids = struct.unpack_from(f"<{size}I", data, offset)
        offset += 4*size
        adjacent = struct.unpack_from(f"<{size}I", data, offset)
        offset += 4*size
        polygons.append(shapely.Polygon(vertices[list(ids)]))
        neighbors.append(adjacent)
    assert offset == len(data) == 128 + vertex_bytes + polygon_bytes
    components = np.full(count, -1, dtype=np.int32)
    component_count = 0
    for first in range(count):
        if components[first] >= 0:
            continue
        pending = [first]
        components[first] = component_count
        while pending:
            for other in neighbors[pending.pop()]:
                if other != 0xffffffff and components[other] < 0:
                    components[other] = component_count
                    pending.append(other)
        component_count += 1
    return shapely.STRtree(polygons), components, component_count


def classify_endpoints(routes, tree, components):
    points = shapely.points(np.array([p for r in routes for p in (r["start"], r["goal"])], dtype=np.float64))
    hits = tree.query(points, predicate="intersects")
    labels = np.full(len(points), -1, dtype=np.int32)
    for point, polygon in zip(*hits):
        component = components[polygon]
        assert labels[point] in (-1, component), "Endpoint touches different components"
        labels[point] = component
    return labels.reshape((-1, 2))


def run(args):
    root, output = Path.cwd(), args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    binaries = output / "binaries"
    binaries.mkdir(exist_ok=True)
    candidate = binaries / "candidate"
    if candidate.exists():
        raise ValueError("Choose a fresh output directory; existing benchmark results are preserved")
    shutil.copy2(args.binary, candidate)
    scene, mesh = args.replay.resolve(), args.navmesh.resolve()
    manifest = {"baseline_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                "scene": str(scene), "scene_sha256": sha(scene), "mesh": str(mesh), "mesh_sha256": sha(mesh),
                "candidate_binary_sha256": sha(candidate), "random_seed": args.seed,
                "cpu_affinity": args.cpu, "warmups_per_process": 1, "measured_passes_per_process": args.repeats,
                "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
                "flags": "-O3 -DNDEBUG"}
    baseline = args.baseline.resolve() if args.baseline else None
    if baseline:
        manifest["baseline_binary_sha256"] = sha(baseline)
    source_files = ["CMakeLists.txt", "tests/navmesh_fixture.hpp"]
    for directory, pattern in (("src", "zombie_navmesh*"), ("tests", "*zombie_navmesh*"),
                               ("tools", "*zombie_navmesh*")):
        source_files.extend(str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern)) if p.is_file())
    for name in source_files:
        target = output / "source-snapshot" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / name, target)
    manifest["source_sha256"] = {name: sha(root / name) for name in source_files}
    (output / "source.diff").write_bytes(subprocess.check_output(["git", "diff", "HEAD", "--binary"]))
    shutil.copy2(__file__, output / "run_comparison.py")
    variants = ["weight-8", "weight-2", "weight-4", "weight-16", "weight-32", "greedy"]
    forward = [(scenario, mode) for mode in variants for scenario in ("formation", "random")]
    order = forward + list(reversed(forward))
    if baseline:
        order = [("formation", "committed-weight8")] + order + [("formation", "committed-weight8")]
    manifest["order"] = order
    (output / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n")
    documents, processes, digests = {}, [], {}
    for number, (scenario, mode) in enumerate(order, 1):
        destination = output / f"{number:02d}-{scenario}-{mode}"
        assert not destination.exists()
        binary = baseline if mode == "committed-weight8" else candidate
        command = ["taskset", "-c", str(args.cpu), str(binary), "--navmesh", str(mesh),
                   "--replay", str(scene), "--batches", "1000", "--repeats", str(args.repeats),
                   "--output", str(destination)]
        command += ["--greedy"] if mode == "greedy" else ["--heuristic-weight", mode.removeprefix("weight-").replace("committed-weight8", "8")]
        if scenario == "random":
            command += ["--random-seed", str(args.seed)]
        profile = output / f"{number:02d}-process.json"
        print(f"Running {number}/{len(order)}: {scenario}, {mode}", flush=True)
        with (output / f"{number:02d}.log").open("w") as log:
            subprocess.run(["/usr/bin/time", "-o", str(profile), "-f",
                            '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_rss_kib":%M}',
                            *command], stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((destination / "paths.json").read_text())
        batch, = document["batches"]
        assert document["replay_sha256"] == manifest["scene_sha256"]
        assert document["search_node_limit"] == 50000 and not document["reopen_closed"]
        assert document["validation"]["endpoint_tolerance_m"] == 0
        assert document["validation"]["navmesh_segments_every_repeat"]
        assert document["validation"]["exact_padded_source_geometry_last_repeat"]
        assert batch["zombies"] == len(batch["routes"]) == 1000
        assert document["priority_formula"] == ("h" if mode == "greedy" else "g + heuristic_weight * h")
        digest = endpoint_digest(batch["routes"])
        assert digests.setdefault(scenario, digest) == digest
        key = scenario, mode
        if key in documents:
            previous = documents[key]["batches"][0]
            for field in ("routes", "stats", "status_counts", "waypoints", "arena_bytes"):
                assert previous[field] == batch[field], (key, field)
        documents[key] = document
        if baseline and scenario == "formation" and mode == "weight-8":
            previous = documents[("formation", "committed-weight8")]["batches"][0]
            for field in ("routes", "stats", "status_counts", "waypoints", "arena_bytes"):
                assert previous[field] == batch[field], ("Baseline control mismatch", field)
        processes.append({"process": number, "scenario": scenario, "variant": mode,
                          "command": command, "output": str(destination), "samples_ms": batch["samples_ms"],
                          "profile": json.loads(profile.read_text())})
        (output / "processes.json").write_text(json.dumps(processes, indent=2) + "\n")
        print(f"  {batch['median_ms']:.3f} ms; {batch['status_counts']}", flush=True)

    example = documents[("formation", "weight-8")]
    obstacles = physical_obstacles(json.loads(scene.read_text()), example["navmesh"])
    polygons, components, component_count = component_index(mesh)
    summary = {"processes": processes, "component_count": component_count, "endpoint_sha256": digests,
               "baseline_routes_and_counters_identical": bool(baseline), "scenarios": {}}
    for scenario in ("formation", "random"):
        reference = documents[(scenario, "weight-2")]["batches"][0]
        labels = classify_endpoints(reference["routes"], polygons, components)
        same = (labels[:, 0] >= 0) & (labels[:, 1] >= 0) & (labels[:, 0] == labels[:, 1])
        disconnected = (labels[:, 0] >= 0) & (labels[:, 1] >= 0) & ~same
        distances = [float(np.linalg.norm(np.array(r["start"])-r["goal"])) for r in reference["routes"]]
        detail = {"same_component_pairs": int(same.sum()), "disconnected_pairs": int(disconnected.sum()),
                  "blocked_endpoint_pairs": int((labels < 0).any(axis=1).sum()),
                  "straight_line_distance_m": {"min": min(distances), "median": statistics.median(distances), "max": max(distances)},
                  "sampling": documents[(scenario, "weight-8")].get("random_sampling"), "variants": {}}
        for mode in variants + (["committed-weight8"] if baseline and scenario == "formation" else []):
            document = documents[(scenario, mode)]
            batch, = document["batches"]
            segments, ratios, failure_components = [], [], {}
            for i, (route, original) in enumerate(zip(batch["routes"], reference["routes"])):
                assert route["start"] == original["start"] and route["goal"] == original["goal"]
                points = route["points"]
                segments.extend(zip(points, points[1:]))
                if route["status"] == "found":
                    assert same[i], (scenario, mode, i, "Found route across components")
                    if original["status"] == "found" and original["length_m"]:
                        ratios.append((route["length_m"]/original["length_m"]-1)*100)
                else:
                    classification = "disconnected" if disconnected[i] else "reachable" if same[i] else "blocked"
                    bucket = failure_components.setdefault(classification, {})
                    bucket[route["status"]] = bucket.get(route["status"], 0) + 1
            collisions = obstacles.query(shapely.linestrings(np.asarray(segments, dtype=np.float64)), predicate="intersects")
            assert collisions.shape[1] == 0, (scenario, mode, "physical collisions")
            samples = [ms for p in processes if p["scenario"] == scenario and p["variant"] == mode for ms in p["samples_ms"]]
            detail["variants"][mode] = {"samples_ms": samples, "median_ms": statistics.median(samples),
                "min_ms": min(samples), "max_ms": max(samples), "status_counts": batch["status_counts"],
                "failure_components": failure_components, "stats": batch["stats"], "waypoints": batch["waypoints"],
                "arena_bytes": batch["arena_bytes"], "memory": document["memory"],
                "independent_segments": len(segments), "independent_collisions": int(collisions.shape[1]),
                "shared_successes_with_weight2": len(ratios), "length_change_vs_weight2_percent": {
                    "median": float(np.median(ratios)), "p95": float(np.percentile(ratios, 95)),
                    "min": min(ratios), "max": max(ratios)}}
        summary["scenarios"][scenario] = detail
    for path, digest in ((scene, manifest["scene_sha256"]), (mesh, manifest["mesh_sha256"]),
                         (candidate, manifest["candidate_binary_sha256"])):
        assert sha(path) == digest, ("Input changed", path)
    if baseline:
        assert sha(baseline) == manifest["baseline_binary_sha256"]
    for name, digest in manifest["source_sha256"].items():
        assert sha(root / name) == digest, ("Source changed", name)
    (output / "comparison.json").write_text(json.dumps(summary, indent=2) + "\n")
    with (output / "measurements.csv").open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["scenario", "variant", "process", "pass", "milliseconds"])
        for process in processes:
            for repeat, ms in enumerate(process["samples_ms"], 1):
                writer.writerow([process["scenario"], process["variant"], process["process"], repeat, ms])
    print(output / "comparison.json", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--navmesh", type=Path, required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--baseline", type=Path)
    parser.add_argument("--seed", type=int, default=1515146562)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=5)
    run(parser.parse_args())
