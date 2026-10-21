#!/usr/bin/env python3
"""Matched weight-8 comparison before and after navmesh connectivity rejection.

Run from the repository root using the geometry-enabled Python environment.
Keeps original workloads, source snapshots, raw measurements, native validation,
and an independent GEOS/connectivity check. Baseline executable is required.
"""
import argparse
import csv
import json
from pathlib import Path
import shutil
import statistics
import subprocess

import numpy as np
import shapely

from benchmark_zombie_navmesh_weights import (sha, endpoint_digest, physical_obstacles,
                                             component_index, classify_endpoints)


def run(args):
    root, output = Path.cwd(), args.output.resolve()
    binaries = output / "binaries"
    binaries.mkdir(parents=True, exist_ok=True)
    candidate = binaries / "candidate"
    if candidate.exists():
        raise ValueError("Choose a fresh output directory; prior results are preserved")
    shutil.copy2(args.binary, candidate)
    baseline, scene, mesh = args.baseline.resolve(), args.replay.resolve(), args.navmesh.resolve()
    source_files = ["CMakeLists.txt", "tests/navmesh_fixture.hpp"]
    for directory, pattern in (("src", "zombie_navmesh*"), ("tests", "*zombie_navmesh*"),
                               ("tools", "*zombie_navmesh*")):
        source_files.extend(str(p.relative_to(root)) for p in sorted((root/directory).glob(pattern)) if p.is_file())
    for name in source_files:
        target = output / "source-snapshot" / name
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root/name, target)
    manifest = {"baseline_commit": subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                "scene": str(scene), "scene_sha256": sha(scene), "mesh": str(mesh), "mesh_sha256": sha(mesh),
                "baseline_binary_sha256": sha(baseline), "candidate_binary_sha256": sha(candidate),
                "random_seed": args.seed, "cpu_affinity": args.cpu, "heuristic_weight": 8,
                "reopen_closed": False, "warmups_per_process": 1, "measured_passes_per_process": args.repeats,
                "compiler": subprocess.check_output(["c++", "--version"], text=True).splitlines()[0],
                "flags": "-O3 -DNDEBUG", "source_sha256": {name: sha(root/name) for name in source_files}}
    (output/"source.diff").write_bytes(subprocess.check_output(["git", "diff", "HEAD", "--binary"]))
    manifest["source_diff_sha256"] = sha(output/"source.diff")
    shutil.copy2(__file__, output/"run_comparison.py")
    forward = [(scenario, variant) for scenario in ("formation", "random") for variant in ("baseline", "components")]
    order = forward + list(reversed(forward))
    manifest["order"] = order
    (output/"manifest.json").write_text(json.dumps(manifest, indent=2)+"\n")
    documents, processes, digests = {}, [], {}
    for number, (scenario, variant) in enumerate(order, 1):
        destination = output / f"{number:02d}-{scenario}-{variant}"
        assert not destination.exists()
        binary = baseline if variant == "baseline" else candidate
        command = ["taskset", "-c", str(args.cpu), str(binary), "--navmesh", str(mesh), "--replay", str(scene),
                   "--heuristic-weight", "8", "--batches", "1000", "--repeats", str(args.repeats),
                   "--output", str(destination)]
        if scenario == "random":
            command += ["--random-seed", str(args.seed)]
        profile = output / f"{number:02d}-process.json"
        print(f"Running {number}/{len(order)}: {scenario}, {variant}", flush=True)
        with (output/f"{number:02d}.log").open("w") as log:
            subprocess.run(["/usr/bin/time", "-o", str(profile), "-f",
                            '{"wall_seconds":%e,"user_seconds":%U,"system_seconds":%S,"peak_rss_kib":%M}',
                            *command], stdout=log, stderr=subprocess.STDOUT, check=True)
        document = json.loads((destination/"paths.json").read_text())
        batch, = document["batches"]
        assert document["replay_sha256"] == manifest["scene_sha256"]
        assert document["heuristic_weight"] == 8 and not document["greedy"] and not document["reopen_closed"]
        assert document["search_node_limit"] == 50000
        assert document["validation"]["navmesh_segments_every_repeat"]
        assert document["validation"]["exact_padded_source_geometry_last_repeat"]
        assert document["validation"]["endpoint_tolerance_m"] == 0
        assert batch["zombies"] == len(batch["routes"]) == 1000
        digest = endpoint_digest(batch["routes"])
        assert digests.setdefault(scenario, digest) == digest
        key = scenario, variant
        if key in documents:
            previous = documents[key]["batches"][0]
            for field in ("routes", "stats", "status_counts", "waypoints", "arena_bytes"):
                assert previous[field] == batch[field], (key, field)
        documents[key] = document
        processes.append({"process": number, "scenario": scenario, "variant": variant,
                          "command": command, "output": str(destination), "samples_ms": batch["samples_ms"],
                          "load_ms": document["navmesh_load_ms"], "setup_ms": document["setup_ms"],
                          "profile": json.loads(profile.read_text())})
        (output/"processes.json").write_text(json.dumps(processes, indent=2)+"\n")
        print(f"  {batch['median_ms']:.3f} ms; {batch['status_counts']}", flush=True)

    example = documents[("random", "components")]
    obstacles = physical_obstacles(json.loads(scene.read_text()), example["navmesh"])
    polygons, components, component_count = component_index(mesh)
    assert component_count == example["navmesh"]["components"] == example["connectivity_precheck"]["components"]
    summary = {"processes": processes, "component_count": component_count, "endpoint_sha256": digests, "scenarios": {}}
    for scenario in ("formation", "random"):
        original, candidate_document = documents[(scenario, "baseline")], documents[(scenario, "components")]
        before, after = original["batches"][0], candidate_document["batches"][0]
        labels = classify_endpoints(before["routes"], polygons, components)
        valid = (labels >= 0).all(axis=1)
        disconnected = valid & (labels[:, 0] != labels[:, 1])
        expected_rejections = int(disconnected.sum())
        assert after["connectivity_rejected_requests"] == expected_rejections
        assert before["arena_bytes"] == after["arena_bytes"] and before["waypoints"] == after["waypoints"]
        zero_counters = ("stored_nodes", "expanded_nodes", "neighbor_tests", "funnel_tests",
                         "corridor_polygons", "peak_queue", "graph_cost_m")
        for i, (a, b) in enumerate(zip(before["routes"], after["routes"])):
            assert a["start"] == b["start"] and a["goal"] == b["goal"]
            if disconnected[i]:
                assert a["status"] in ("no_path", "search_limit") and b["status"] == "no_path"
                assert all(b["stats"][field] == 0 for field in zero_counters)
                assert b["stats"]["location_tests"] == a["stats"]["location_tests"]
                assert not a["points"] and not b["points"] and a["path_id"] == b["path_id"] == 0
            else:
                assert a == b, ("Connected/blocked route changed", scenario, i+1)
        delta = candidate_document["memory"]["core_payload_bytes"] - original["memory"]["core_payload_bytes"]
        assert delta == candidate_document["memory"]["connectivity_bytes"] == 4*example["navmesh"]["polygons"]
        for field in original["memory"]:
            if field != "core_payload_bytes":
                assert original["memory"][field] == candidate_document["memory"][field]
        detail = {"reachable_and_blocked_routes_and_counters_identical": True,
                  "same_component_pairs": int((valid & ~disconnected).sum()),
                  "disconnected_pairs": expected_rejections, "blocked_endpoint_pairs": int((~valid).sum()),
                  "variants": {}}
        for variant in ("baseline", "components"):
            document = documents[(scenario, variant)]
            batch, = document["batches"]
            segments = [segment for route in batch["routes"] for segment in zip(route["points"], route["points"][1:])]
            collisions = obstacles.query(shapely.linestrings(np.asarray(segments, dtype=np.float64)), predicate="intersects")
            assert collisions.shape[1] == 0
            samples = [ms for p in processes if p["scenario"] == scenario and p["variant"] == variant for ms in p["samples_ms"]]
            detail["variants"][variant] = {"samples_ms": samples, "median_ms": statistics.median(samples),
                "min_ms": min(samples), "max_ms": max(samples), "status_counts": batch["status_counts"],
                "connectivity_rejected_requests": batch.get("connectivity_rejected_requests", 0),
                "stats": batch["stats"], "arena_bytes": batch["arena_bytes"], "waypoints": batch["waypoints"],
                "memory": document["memory"], "independent_segments": len(segments), "independent_collisions": 0}
        summary["scenarios"][scenario] = detail
    for path, digest in ((scene, manifest["scene_sha256"]), (mesh, manifest["mesh_sha256"]),
                         (baseline, manifest["baseline_binary_sha256"]), (candidate, manifest["candidate_binary_sha256"])):
        assert sha(path) == digest, ("Input changed", path)
    for name, digest in manifest["source_sha256"].items():
        assert sha(root/name) == digest, ("Source changed", name)
    (output/"comparison.json").write_text(json.dumps(summary, indent=2)+"\n")
    with (output/"measurements.csv").open("w", newline="") as stream:
        writer = csv.writer(stream, lineterminator="\n")
        writer.writerow(["scenario", "variant", "process", "pass", "milliseconds"])
        for process in processes:
            for repeat, ms in enumerate(process["samples_ms"], 1):
                writer.writerow([process["scenario"], process["variant"], process["process"], repeat, ms])
    print(output/"comparison.json", flush=True)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, required=True)
    parser.add_argument("--baseline", type=Path, required=True)
    parser.add_argument("--navmesh", type=Path, required=True)
    parser.add_argument("--replay", type=Path, required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--seed", type=int, default=1515146562)
    parser.add_argument("--cpu", type=int, default=2)
    parser.add_argument("--repeats", type=int, default=5)
    run(parser.parse_args())
