# Zombie navmesh navigation

Navigation development now lives in this repository on branch
`implement-zombie-navmesh-navigation`, created from master `4080057`.
This ports the existing navmesh solution from experimental commit `338a293`;
search behavior and storage are preserved. The proposed further optimizations
are separate future work.

The latest tree-free experiment found **999 of 1,000 paths in 162.026 ms**
(median of five measured batches after one warm-up). Its only rejection was a
destination inside a padded fence. Navigation allocations totaled **21.522 MB**,
including the full 10 MB path arena. These are historical measurements from the
[matched comparison](ZOMBIE_PATHFINDING_NO_TREES.md), rather than new timings for
this port. The [original navmesh report](ZOMBIE_PATHFINDING_NAVMESH_EXPERIMENT.md)
preserves the earlier experiment with tree obstacles included.

The implementation is a headless library and benchmark. The viewer's existing
live crowd behavior is unchanged; runtime crowd integration is subsequent work.

## Query behavior and ownership

The offline baker subtracts expanded, oriented obstacle rectangles from the
map boundary and triangulates the free space in tiles with matching portals.
Runtime requests search adjacent triangles using weighted A*, then funnel the
shared-edge corridor into straight waypoint segments. The selected setting is
`g + 8h`, with each closed triangle expanded at most once. It produces approximate
routes without a shortest-path guarantee. API defaults remain weight 2; request
weight 8 explicitly to reproduce the selected variant.

Coordinates are double-precision **X east / Z south in meters**; this is planar
navigation. Obstacles are expanded by a 0.300 m zombie radius, 0.002 m clearance,
0.050 m extra for buildings, and a separate 0.005 m numerical margin. Roads are
ordinary free space. Terrain slope, bridges, moving obstacles and changing doors
are not represented. Trees are ignored by omitting kind `1` from the bake input.

Each zombie searches independently. A preallocated generation-stamped hash
table, indexed priority queue, records, corridor and funnel scratch are reused
between synchronous requests. The **50,000-node cap** counts distinct stored
triangles per request, including queued triangles. Normal requests allocate no
memory and do not reuse another zombie's route.

`PathManager` owns the zombie-ID → path-ID mapping and an exactly
**10,000,000-byte waypoint arena**, sufficient for 625,000 double X/Z points.
The shared immutable mesh must outlive every search or manager that borrows it.
Borrowed path views last until that path is released, successfully replaced, or
the manager is cleared. Failed replacement retains the previous path.

```cpp
#include "zombie_navmesh_paths.hpp"

using namespace viewer::navmesh_path;
auto mesh = Mesh::load("build/navigation/fixtures/no-trees/cache/world.navmesh");
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
float32 endpoints, promoted to doubles. It never snaps blocked endpoints.

The versioned binary records clearance settings and the **SHA-256 of the exact
input file bytes**. The benchmark rejects a replay whose hash differs from the
mesh. Editing or reserializing a replay requires rebaking its matching mesh.
Publication is atomic after topology, boundaries and area validation.

The retained tree-free fixture is
`build/navigation/fixtures/no-trees/scene/paths.json`, with its aerial image in
that directory and `world.navmesh` plus its JSON sidecar in the sibling `cache/`
directory. The replay SHA-256 is
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
    --navmesh build/navigation/fixtures/no-trees/cache/world.navmesh \
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

The benchmark times independent serial requests, search resets, funnel, path
storage, zombie mapping and status/stat collection. Loading, bake, initial
allocation, batch clearing, validation and image output are outside the timer.
Every successful path is checked against mesh coverage on every pass and padded
source obstacles on the last pass. Tests cover geometry, binary validation,
search/funnel behavior, bounded zero-allocation queries and arena ownership.

## Port verification

All 1,000 complete route records (including waypoint coordinates), status counts,
search counters and memory totals exactly match the retained reference. The 14
implementation, tool and test files are byte-identical to source commit `338a293`.

All four focused CTest suites passed in the new checkout. Replaying the same
1,000 requests with weight 8 produced **999 found / 1 blocked**, with a median
batch time of **193.808 ms** across five passes (183.208–213.407 ms). The new
run retained 696,737 stored nodes, 519,578 expansions, 379,265 corridor triangles
and 38,629 waypoints. These match the historical search totals. The port changes
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
