# Early rejection of disconnected navmesh requests

**The same 1,000 random requests now take 208.297 ms,
down from a freshly matched 539.152 ms: 2.588× faster
and 61.37% less elapsed query time.** All 960 successful routes remain
exactly unchanged. The 40 impossible requests return `no_path` before storing
or expanding a search node. Shared memory increases by **321,108 B (0.321 MB)**.

The selected search remains **`g + 8h`**, no closed-polygon reopening,
50,000 distinct stored polygons per request, and the exact 10,000,000-byte path
arena. Every zombie still searches independently.

## Implementation

`Mesh` labels each polygon with a 32-bit connected-component ID once during
loading/construction, after all neighbor and reciprocal-portal checks pass.
An iterative depth-first traversal follows only validated neighbor edges;
marking polygons before pushing visits each polygon once. Shared vertices
without a portal do not connect components. Construction is O(polygons + edges)
and is independent of the 50,000-node query limit. Its temporary traversal
stack is freed after initialization.

All legacy triangle and convex polygon constructors/loaders build these labels;
the default empty mesh has zero components. There is no binary format change,
rebake, obstacle removal or modification of polygon geometry/adjacency.

Requests still validate finite inputs and locate both endpoints first. Blocked
endpoints retain their existing status. For two valid polygons, differing IDs
return `Status::noPath` before computing search priority or inserting records:

```cpp
if (mesh.component(firstPolygon) != mesh.component(goalPolygon))
    return result(Status::noPath);
```

Rejected requests retain endpoint-location statistics, with zero stored nodes,
expansions, queue size, neighbor/funnel tests, corridor length and graph cost.
Same-component requests use the existing search unchanged and can still hit
their node cap. Normal requests allocate no memory; failed replacements retain
the zombie's previously owned path and mapping.

Labels describe the baked static portal graph and are rebuilt when a new mesh
loads. Mesh connectivity changes require recomputing them; doors and moving
blockers absent from the mesh remain outside this static check.

## Matched benchmark

The previous priority-sweep work is committed as **`6df5cb8`**,
and its frozen executable is the baseline. Both executables use the exact same
four-vertex, tree-free mesh: 80,277 polygons, 113,326 vertices, 292,487 directed
edges and **343 connected components**. The input remains 9,981 building and
14,248 fence boxes; all 147,495 tree trunks remain ignored.

- **Formation:** the previous 1,000 relative-offset requests, 5,520.682 m north.
  All 999 connected pairs succeed; the same one destination is blocked.
- **Random:** the previous 1,000 independent mapwide walkable start/goal pairs,
  seed 1515146562, with no snapping or component filtering. Exact pair SHA-256
  matches the earlier weight sweep. There are 960 connected and 40 disconnected
  pairs, independently confirmed from mesh adjacency and GEOS endpoint location.

GCC 14.2 Release (`-O3 -DNDEBUG`), Ryzen 5 7535HS, serial requests pinned to CPU 2.
The eight process order is formation baseline/candidate, random baseline/candidate,
then the full order reversed. Each process has one warm-up and five measured
1,000-request passes: **ten samples per table entry**, 40 measured passes total.
All successful paths remain live throughout each pass. No samples are excluded.

| Workload | Baseline median, ms | Components median, ms | Baseline min–max, ms | Components min–max, ms |
| --- | ---: | ---: | ---: | ---: |
| Formation | 69.151 | 62.645 | 60.961–83.511 | 60.602–78.664 |
| Random | 539.152 | 208.297 | 472.445–738.936 | 172.336–271.904 |

The random improvement is substantial across the observed ranges. Formation
work is identical and ranges overlap; its lower candidate median does not
establish a reliable speed improvement attributable to this check. Earlier
priority-sweep/merging times are historical; the table uses fresh controls.
Whole-process CPU time again totals about half elapsed time for an unresolved
environmental reason. No CPU-ratio normalization is applied.

Timing includes independent searches/resets, funnel, path-arena allocation,
zombie-ID mapping and status/stat collection. It excludes mesh load/component
construction, initial workspace allocation, input/random generation, batch
clearing, validation, serialization and image rendering. Load/setup timings and
whole-process profiles are retained separately in `comparison.json`.

| Random statuses | Baseline | Components |
| --- | ---: | ---: |
| Found | 960 | 960 |
| `no_path` | 20 | 40 |
| `search_limit` | 20 | 0 |
| Blocked / invalid / arena full | 0 | 0 |
| Rejected by connectivity before search | 0 | 40 |

The original 20 capped failures are not counted as successes; they are now
conclusive `no_path` results. No disconnected pairs are dropped from timing.

| Work across 1,000 random requests | Baseline | Components |
| --- | ---: | ---: |
| Stored Nodes | 1,471,945 | 469,227 |
| Expanded Nodes | 1,318,881 | 319,272 |
| Neighbor Tests | 4,827,257 | 1,174,528 |
| Corridor Polygons | 204,399 | 204,399 |
| Funnel Tests | 334,107 | 334,107 |
| Location Tests | 21,666 | 21,666 |
| Peak Queue | 393 | 393 |

Expansions decrease **75.79%**, by eliminating 999,609 failed-request
expansions. Successful routes still require the same search and funnel work,
so expansion savings need not equal the elapsed-time savings.

## Routes, memory and validation

All **999 formation and 960 random successful route records match baseline
exactly**, including endpoint/waypoint doubles, path IDs, lengths and search
counters. The blocked formation request also matches exactly. Each variant's
results are deterministic between its two processes. No collision checks or
geometry tolerances are weakened.

Native checks verify endpoints, mapping and navmesh segment coverage every
repetition, strict source-OBB clearance on the last repetition, and complete
arena reclamation. Independent GEOS checks validate **122,704 segments**
across both variants/workloads: **zero intersections** with physical obstacle
padding (0.352 m buildings, 0.302 m fences). The mesh retains its additional
0.005 m numeric margin. The independent graph traversal agrees on all 343
components, all 40 rejected pairs, and all 960 reachable random pairs.

| Navigation buffer payload | Baseline | Components |
| --- | ---: | ---: |
| Shared component labels | 0 B | 321,108 B |
| Search workspace | 3,442,472 B | 3,442,472 B |
| Path arena capacity | 10,000,000 B | 10,000,000 B |
| Other shared mesh/cache/index and manager buffers | 9,596,068 B | 9,596,068 B |
| **Total** | **23,038,540 B** | **23,359,648 B** |

The label buffer is shared across all zombies; it is not repeated per search
or stored path. Buffer accounting excludes temporary loader traversal storage,
benchmark validation/JSON/raster data, allocator metadata and process overhead.
Live waypoint usage remains 444,784 B for formation and 568,192 B for random.

All **six focused CTest suites pass**. New regressions cover portal-connected
versus isolated/vertex-touching polygons, v1/v2 loader equivalence, a 50,001-node
component, default empty mesh, all weighted/greedy/reopening search modes,
zero-work cross-component rejection even with a one-record cap, finite/blocked
status precedence, same-component cap enforcement, repeated state resets,
no per-request allocations and retained manager paths after failed replacement.

## Reproduction and saved artifacts

The cache format and geometry inputs are unchanged. From this repository, with
the headless build and Python geometry environment from
[the navigation guide](ZOMBIE_PATHFINDING.md):

```bash
# Current random workload. Omit --random-seed to run the recorded formation.
build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --heuristic-weight 8 --random-seed 1515146562 \
    --batches 1000 --repeats 5 --output build/navigation/random-components

# Matched comparison; preserve the previous executable before rebuilding.
.venv/bin/python tools/benchmark_zombie_navmesh_connectivity.py \
    --binary build/navmesh-navigation/benchmark_zombie_navmesh \
    --baseline build/navigation/connectivity/20261021T105643.265107Z/binaries/baseline \
    --navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --output build/navigation/connectivity/new-run
```

The complete run is `build/navigation/connectivity/20261021T105643.265107Z`. It retains frozen executables,
source snapshots/diff/hashes, commands, profiles, exact endpoints/routes,
native validation, independent connectivity/geometry results and all samples
in `manifest.json`, `processes.json` and `comparison.json`.

- [All 40 measured samples](zombie-navmesh-connectivity-measurements.csv).
- [Formation aerial paths](../build/navigation/connectivity/20261021T105643.265107Z/02-formation-components/zombie_paths_1000.png).
- [Random aerial paths and 40 disconnected requests](../build/navigation/connectivity/20261021T105643.265107Z/04-random-components/zombie_paths_1000.png).

Image headers show the five-pass median of their individual process; tables
pool both processes into ten samples. Route geometry is identical between repeats.

Replay SHA-256: `a0693058d0172c7d434b96a5a2fed51e13c6b495a4f62df1dc90ddc6a5f10fbe`.
Mesh SHA-256: `644ba9efb20de75091e1ddd059f3e258e45b13348320683359dbee16d30be200`.
Endpoint pair SHA-256: formation `e56556e0a9bd7baac260a880e5564c83a7ac046e881606df966cde52093a0bfe`;
random `e9de16c8e2678ddc8d0e2fc38dd1b4adba5f512dacbd831c4bb8a8b249df352e`.
Generated caches, raster images and result JSON remain ignored build artifacts.
