# Zombie navmesh navigation

Navigation development now lives in this repository on branch
`implement-zombie-navmesh-navigation`, created from master `4080057`.
The navmesh solution was ported from experimental commit `338a293` in commit
`6dc4588`, with query optimizations 1–3 committed as `3188642`. Convex polygon
merging is now implemented and benchmarked. The selected setting merges up to
**four vertices per region**, now the merger default, while preserving the
original walkable space.

The earlier merging comparison finds **999 of 1,000 paths in 55.847 ms**, versus **110.839 ms**
for the freshly rerun committed triangle baseline: **1.985× faster**. Navigation
allocations fall from **26.282 MB to 23.039 MB**, including the full 10 MB path
arena. The same one destination remains blocked by a fence. These are medians
of ten measured crowd passes per variant; exact route shapes change under merging.
See the [convex merging investigation](ZOMBIE_PATHFINDING_CONVEX_MERGING.md) for
the four/six-vertex comparison, path lengths, validation and memory.

The [query optimization benchmark](ZOMBIE_PATHFINDING_OPTIMIZATIONS.md),
[three-method comparison](ZOMBIE_PATHFINDING_NO_TREES.md), and
[original report with trees](ZOMBIE_PATHFINDING_NAVMESH_EXPERIMENT.md) retain their
historical measurements, which are separate from the fresh merging comparison.

The [priority sweep and random-destination benchmark](ZOMBIE_PATHFINDING_WEIGHT_SWEEP.md)
compares weights 2, 4, 8, 16, 32 and pure greedy on the four-vertex tree-free mesh.
That sweep measured **61.150 ms** for weight 8 on the existing formation and
**481.114 ms** for weight 4 on 1,000 mapwide random pairs. All settings find
999 formation paths and all 960 reachable random paths; the other 40 random
pairs span disconnected regions. Timing ranges overlap, so these results do
not establish a universal best weight. Increasing greediness gives no reliable
further gain here.

The selected priority remains **`g + 8h`**. A new
[connectivity precheck](ZOMBIE_PATHFINDING_CONNECTIVITY.md) rejects requests
between disconnected mesh regions before starting search. On the same random
workload, fresh medians fall from **539.152 ms to 208.297 ms**
(**2.588× faster**), preserving every successful route exactly. All 40
impossible requests now return `no_path` with zero search records/expansions;
there are no random search-limit failures. Shared labels add **321,108 B**,
bringing current navigation buffers to **23,359,648 B (23.360 MB)**. The formation
still finds the same 999 paths, with its one blocked destination unchanged.

The implementation is a headless library and benchmark. The viewer's existing
live crowd behavior is unchanged; runtime crowd integration is subsequent work.

The child branch `experiment-zombie-local-steering` compares five ways to follow
these tree-free paths around trunks. In its synthetic 1,000-NPC woodland test,
Detour avoidance reaches 998 destinations and the analytical gradient reaches
994. See the [local steering experiment](ZOMBIE_STEERING_EXPERIMENT.md) for the
shared collision guard, timings, failures and trajectory images. This is a
headless experiment; it does not change live crowd behavior.

The fourth original experiment workspace, `project-gdll-potential-surface`,
tests **Potential Surface Steering without a guiding path** on the actual map.
With all buildings, fences and trees included, 56 of 1,000 random agents arrive
and 944 stall. Its standalone update loop uses 1.225 ms CPU per tick with 1,000
active agents. See the [goal-directed surface experiment](POTENTIAL_SURFACE_NAVIGATION.md)
for geometry, stopping rules, timing scope and aerial trajectory images.

## Query behavior and ownership

The offline baker subtracts expanded, oriented obstacle rectangles from the
map boundary and triangulates the free space in tiles with matching portals.
An offline postprocessor merges adjacent convex regions without moving vertices.
Runtime requests search adjacent polygons using weighted A*, then funnel the
shared-edge corridor into straight waypoint segments. The selected setting is
`g + 8h`, with each closed polygon expanded at most once. It produces approximate
routes without a shortest-path guarantee. API defaults remain weight 2; request
weight 8 explicitly to reproduce the selected variant.

Coordinates are double-precision **X east / Z south in meters**; this is planar
navigation. Obstacles are expanded by a 0.300 m zombie radius, 0.002 m clearance,
0.050 m extra for buildings, and a separate 0.005 m numerical margin. Roads are
ordinary free space. Terrain slope, bridges, moving obstacles and changing doors
are not represented. Trees are ignored by omitting kind `1` from the bake input.

Each zombie searches independently. A preallocated generation-stamped array
indexes search records directly by polygon ID. The lookup, indexed priority
queue, records, corridor and funnel scratch are reused between synchronous
requests. The **50,000-node cap** counts distinct stored polygons per request,
including queued polygons. Normal requests allocate no memory and do not reuse
another zombie's route.

At mesh load/construction, an iterative traversal labels every polygon's
connected component from validated reciprocal portals. After locating valid
endpoints, the search compares their labels and returns `noPath` immediately
if they differ. Equal labels proceed through normal independent search and
retain the same node cap. Labels describe the static baked graph and rebuild
when a new mesh loads; changes to connectivity require recomputing them.
There is no new cache format or rebake requirement for this optimization.

The immutable mesh caches double-precision polygon centers and neighbor graph
distances once at load/construction: 16 bytes per polygon plus eight per directed
edge, shared by all searches. Component labels use another four bytes per polygon,
also shared. The direct lookup uses eight bytes per mesh polygon
per search workspace; its size follows the mesh, independently of the
50,000-record cap.
Default searches use the goal record's existing graph cost. The reopening
reference mode still sums the reconstructed corridor because ancestor costs
can change after descendants were processed.

`PathManager` owns the zombie-ID → path-ID mapping and an exactly
**10,000,000-byte waypoint arena**, sufficient for 625,000 double X/Z points.
The shared immutable mesh must outlive every search or manager that borrows it.
Borrowed path views last until that path is released, successfully replaced, or
the manager is cleared. Failed replacement retains the previous path.

```cpp
#include "zombie_navmesh_paths.hpp"

using namespace viewer::navmesh_path;
auto mesh = Mesh::load("build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh");
PathManager paths(mesh, 1000, 50'000, 8.0);
auto id = paths.request(zombieId, Point{startX, startZ}, Point{goalX, goalZ});
if (id != invalidPath) {
    auto waypoints = paths.view(id);
    // Follow consecutive waypoint segments; retain mesh and paths while using them.
} else {
    auto status = paths.lastStatus();
    // Handle a blocked endpoint, search limit, or another failure.
}
```

## Bake and replay input

`tools/bake_zombie_navmesh.py` consumes a recorded JSON scene containing:

- `obstacles`: arrays `[kind, x, z, ux, uz, hx, hz]`, where kinds are building `0`,
  trunk `1`, fence `2`; `[ux, uz]` gives the rectangle's first axis and `[hx, hz]`
  its half extents. The baker normalizes the axis.
- `raster.minimum` and `raster.maximum`: `[x, z]` map bounds in meters.

The benchmark additionally reads `batches[].routes[]` with `start` and `goal`
X/Z pairs from the largest recorded batch, and `raster.file` for its aerial
background relative to the replay file. Existing requests retain their runtime
float32 endpoints, promoted to doubles. It never snaps blocked endpoints. With
`--random-seed UINT32`, the benchmark instead generates independent, mapwide,
walkable start/goal pairs after verifying the original geometry replay hash.
Seed zero is valid. Disconnected pairs are retained, and generation is untimed.
`--greedy` selects pure `h` priority and cannot be combined with an explicit
`--heuristic-weight`. Exact endpoints and sampling metadata are saved in the
result. See the sweep report for the complete comparison command.

The loader accepts the original triangle `GDNAV01` and variable-edge polygon
`GDNAV02` formats. The versioned binary records clearance settings and the
**SHA-256 of the exact input file bytes**. The benchmark rejects a replay whose hash differs from the
mesh. Editing or reserializing a replay requires rebaking its matching mesh.
Publication is atomic after topology, boundaries and area validation.

The retained tree-free fixture is
`build/navigation/fixtures/no-trees/scene/paths.json`, with its aerial image in
that directory and `world.navmesh` plus its JSON sidecar in the sibling `cache/`
directory. The selected merged cache is `cache/world-convex-4.navmesh`.
The replay SHA-256 is
`a0693058d0172c7d434b96a5a2fed51e13c6b495a4f62df1dc90ddc6a5f10fbe`.
Generated fixtures and outputs remain ignored build artifacts.

## Build, verify and run

Run from this repository. The existing `.venv` supplies NumPy, Shapely 2.1+ and
Pillow; no Python or graphics context is needed for runtime queries. Configure a
separate headless build using the local dependency cache:

```bash
cmake -S . -B build/navmesh-navigation \
    -DGD_VIEWER=OFF -DGD_MULTIPLAYER=OFF -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$PWD/.venv/bin/python" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_GLM="$PWD/build/_deps/glm-src" \
    -DFETCHCONTENT_SOURCE_DIR_TINYGLTF="$PWD/build/_deps/tinygltf-src" \
    -DFETCHCONTENT_SOURCE_DIR_BOX3D="$PWD/build/_deps/box3d-src" \
    -DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER="$PWD/build/meshoptimizer-1.3" \
    -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="$PWD/build/_deps/tomlplusplus-src"
cmake --build build/navmesh-navigation --target benchmark_zombie_navmesh \
    test_zombie_navmesh test_zombie_navmesh_search test_zombie_navmesh_paths -j4
ctest --test-dir build/navmesh-navigation -R '^viewer_zombie_navmesh' --output-on-failure

# Reuse the matching precomputed mesh for the retained fixture.
taskset -c 2 ./build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --heuristic-weight 8 --batches 1000 --repeats 5 \
    --output build/navigation/results/no-trees
.venv/bin/python tools/render_zombie_navmesh_paths.py \
    build/navigation/results/no-trees/paths.json
```

To bake a new cache from a prepared scene, use a new output path:

```bash
.venv/bin/python tools/bake_zombie_navmesh.py \
    --scene build/navigation/fixtures/no-trees/scene/paths.json \
    --output build/navigation/results/rebake/world.navmesh --tile-size 256
```

To merge a triangle cache without rebaking the town:

```bash
.venv/bin/python tools/merge_zombie_navmesh.py \
    --input build/navigation/fixtures/no-trees/cache/world.navmesh \
    --output build/navigation/results/merged-4.navmesh --max-vertices 4
```

The benchmark times independent serial requests, search resets, funnel, path
storage, zombie mapping and status/stat collection. Loading, bake, initial
allocation, batch clearing, validation and image output are outside the timer.
Every successful path is checked against mesh coverage on every pass and padded
source obstacles on the last pass. Tests cover geometry, binary validation,
search/funnel behavior, bounded zero-allocation queries and arena ownership.

## Historical port verification (`6dc4588`)

Before the query optimizations, all 1,000 complete route records (including
waypoint coordinates), status counts, search counters and memory totals exactly
matched the retained reference. At port commit `6dc4588`, the 14 implementation,
tool and test files were byte-identical to source commit `338a293`.

All four focused CTest suites passed in the new checkout. Replaying the same
1,000 requests with weight 8 produced **999 found / 1 blocked**, with a median
batch time of **193.808 ms** across five passes (183.208–213.407 ms). The new
run retained 696,737 stored nodes, 519,578 expansions, 379,265 corridor triangles
and 38,629 waypoints. These match the historical search totals. That port changed
no search algorithm; elapsed timings from separate runs do not establish an
optimization speedup or regression.

The [port measurement CSV](zombie-navmesh-port-check-measurements.csv) and
[recorded port paths](../build/navigation/port-check/paths.json) preserve the
verification run.

Original matched-run evidence is retained separately under
`build/navigation/reference/no-trees/`: the
[run manifest](../build/navigation/reference/no-trees/results/manifest.json),
[comparison data](../build/navigation/reference/no-trees/results/comparison.json),
[navmesh paths](../build/navigation/reference/no-trees/results/navmesh/paths.json)
and [aerial comparison](../build/navigation/reference/no-trees/results/comparison_paths.png).
Their original commit IDs and timings describe the archived experiment.
