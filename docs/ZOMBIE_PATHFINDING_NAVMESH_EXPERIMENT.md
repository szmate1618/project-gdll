# Navmesh zombie pathfinding experiment

> Historical report from experimental commit `338a293`, with tree obstacles
> included. See [the current navigation entry](ZOMBIE_PATHFINDING.md) for the
> port to this repository and the later tree-free result. The reproduction
> commands below retain their original worktree context; archived artifacts
> remain in `../../gdll3/project-gdll-navmesh/` relative to this document.

**The selected navmesh search finds 978 of 1,000 paths in a median 1.101 s.**
The other 22 destinations overlap body-expanded obstacles. No request reaches
the search limit. A fresh weighted-JPS control finds 943 paths in 3.541 s:
the navmesh is **3.22× faster** in these measured query batches, recovers 35
endpoints rejected by the raster, and uses **60.08 MB instead of 33.29 MB**.
Among the 943 shared successes, navmesh routes are **3.47% longer at the
median**, with a maximum increase of 12.24%.

This experiment lives in sibling worktree `project-gdll-navmesh`, on branch
`experiment-zombie-navmesh`, forked directly from `6a3647d`. The continuous
planner and grid/JPS experiments remain on their respective sibling branches.
This is a headless navigation library and benchmark; it is not wired into the
viewer's live crowd simulation.

## Algorithm and storage

The offline Python baker subtracts body-expanded, oriented building, trunk,
and fence rectangles from the map boundary, then performs constrained Delaunay
triangulation of the remaining polygons. It uses 256 m bake tiles to keep
triangulation tractable with the forest's many holes. Tile edges are joined
into one conforming mesh; a tile is not a runtime occupancy cell. There is
no half-meter raster in this experiment.

The shared mesh stores double-precision X/Z vertices, three vertex IDs and
three neighbor IDs per triangle. A BVH finds the triangles containing the
exact requested start and destination. Each zombie then searches independently:

1. Weighted A* searches adjacent triangles using `g + weight * h`. Graph edge
   cost is Euclidean distance between triangle centroids; `h` is Euclidean
   distance to the destination triangle's centroid.
2. The selected setting is **weight 8, with each triangle expanded at most
   once**. Open records can improve; closed records are skipped. This is an
   approximate search with no shortest-path or path-stretch guarantee.
3. The reconstructed shared-edge corridor goes through a portal funnel to
   produce straight-line waypoint segments with the exact supplied endpoints.
   The funnel tightens the chosen corridor; the centroid graph does not find
   a globally shortest geometric route, even at weight 1.

The 50,000-node cap means **distinct stored triangles per request**, including
the open queue. It is independent of the mesh's total triangle count. The
generation-stamped hash table has 131,072 slots; records, indexed priority
queue, corridor and funnel scratch are preallocated. Normal queries allocate
no memory. Searches are synchronous and serial, reusing one workspace without
sharing routes or previous search results.

`PathManager` keeps the zombie-ID → path-ID mapping and stores persistent
waypoints in an exactly **10,000,000-byte arena**. A double X/Z waypoint takes
16 bytes, allowing 625,000 points. Paths can be released and their spans reused;
failed replacement preserves the previous path. Borrowed views remain valid
until that path is released, replaced successfully, or the manager is cleared.

The API and benchmark default to weight 2. The selected measured setting is
explicitly requested with weight 8:

```cpp
auto mesh = viewer::navmesh_path::Mesh::load("build/navmesh/world.navmesh");
viewer::navmesh_path::PathManager paths(mesh, 1000, 50000, 8.0);
auto id = paths.request(zombieId, start, goal);
auto waypoints = paths.view(id); // Check lastStatus() if id == invalidPath.
```

## Scene and bake

The input is the existing relative-offset scene snapshot, SHA-256
`886833659e1b1f12488eafb1830240ebb6501c43814dc447ecd8474bf0a90047`.
It contains 9,981 building boxes, 147,495 tree trunks and 14,248 fence panels:
171,724 obstacles in total. The actual map is approximately 6 × 6 km.

All 1,000 starts and all 1,000 distinct destinations are preserved, including
their initial formation offsets. The lower-right starting formation travels
approximately 5.52 km north toward the upper-right destinations. The shorter
batches use prefixes of those requests. Endpoints retain the original runtime
float coordinates, represented exactly as doubles. No endpoint is moved,
snapped or resampled.

Obstacle expansion is 0.300 m zombie radius + 0.002 m clearance, with another
0.050 m for buildings, matching the physical checks in the prior experiments.
The bake adds a further **0.005 m numerical margin** so funnel turns on mesh
boundaries remain safely outside the physical collision boxes. Square expansion
of oriented rectangles is conservative around their corners. Roads are free
space unless another obstacle blocks them; they have no preferred travel cost.
Terrain slope, bridges, changing doors and moving obstacles are not modeled.

| Baked mesh | Value |
| --- | ---: |
| Vertices, double X/Z | 699,431 |
| Triangles | 1,005,563 |
| Connected components | 346 |
| Bake tiles | 576 |
| Free area | 32,987,152.662 m² |
| Binary file, including header | 35,324,536 bytes |
| Offline bake wall time | 44.83 s |
| Offline peak RSS | 639.88 MB |

The forest accounts for much of the mesh complexity. A monolithic triangulation
was too slow in the feasibility test, so the production bake uses tiles. It
checks winding, reciprocal portals, nonmanifold edges, tile seams, triangle
coverage area and every open edge against obstacles or the map boundary.
The 346 components match an independently computed global free-space polygon
decomposition. The versioned binary contains the input SHA-256, bounds and
clearance settings. Invalid or fully blocked inputs preserve the existing file.

## Measurements

Release GCC 14.2 (`-O3 -DNDEBUG`), AMD Ryzen 5 7535HS, CPU 2 affinity.
Each full batch uses one warm-up followed by five measured passes. The timed
interval includes request reset, triangle search, funnel, arena allocation,
zombie mapping and status/stat collection. It excludes mesh loading, initial
allocation, batch clearing, validation, JSON and image output.

Selected navmesh setting: **`g + 8h`, closed triangles are not reopened**.

| Zombies | Median batch | Min–max | Found | Blocked endpoint | Search limit |
| ---: | ---: | ---: | ---: | ---: | ---: |
| 1 | 0.427 ms | 0.358–0.440 ms | 1 | 0 | 0 |
| 10 | 11.364 ms | 8.617–12.456 ms | 10 | 0 | 0 |
| 100 | 102.265 ms | 99.517–116.932 ms | 98 | 2 | 0 |
| 1,000 | 1,101.203 ms | 1,079.241–1,214.032 ms | 978 | 22 | 0 |

The 1,000-request samples were 1.184164, 1.214032, 1.080644, 1.101203 and
1.079241 s. The mean time per submitted request, using the median batch, is
**1.101 ms**, including the 22 early endpoint rejections.

| 1,000-request setting | Measured passes | Median batch | Found | Blocked | Limit |
| --- | ---: | ---: | ---: | ---: | ---: |
| Navmesh, weight 2, reopening | 1 | 26,305.955 ms | 978 | 22 | 0 |
| Navmesh, weight 2, no reopening | 5 | 4,551.112 ms | 968 | 22 | 10 |
| Navmesh, weight 4, no reopening | 1 | 1,298.818 ms | 978 | 22 | 0 |
| Navmesh, weight 8, no reopening | 5 | 1,101.203 ms | 978 | 22 | 0 |
| Grid/JPS, weight 2, fresh control | 5 | 3,541.357 ms | 943 | 57 | 0 |

The one-pass controls also had one warm-up; a single measured sample is not a
five-pass median. Higher weight strongly reduces exploration on this map.
Default weight 2 is retained as a reproducible comparison, rather than silently
changing the search priority between runs.

The earlier grid report's 1.752 s was recorded under different CPU availability.
The comparison above uses a fresh control with identical inputs, affinity,
warm-up count and repeat count. Its range was 3.445–3.606 s. These are wall-clock
query timings on a shared machine; speed ratios describe these runs. Both
primary benchmark commands received 49% CPU over their full execution. Each
reported setting uses a separate completed output directory; an earlier
interrupted run's mixed auxiliary output was discarded and rerun.

The selected navmesh benchmark command took **62.49 s wall / 31.07 s user CPU /
0.14 s system CPU**, including all four batches, warm-ups, repeated validation,
loading and output. Mesh loading and BVH construction took **1,334.956 ms**;
manager construction took **9.389 ms**. Expensive mesh segment verification
runs outside the query timings. The fresh grid's 1,000-request-only control
command took 22.59 s wall / 11.20 s user / 0.09 s system. Its grid loading took
22.004 ms and manager construction took 10.282 ms. Rendering was separate.

## Failures, route quality and search work

All 22 navmesh failures are goals inside physical body-expanded obstacles;
none are caused solely by the additional 5 mm margin. All starts are valid.
There are no `no_path`, `invalid_input` or `buffer_full` results. All 943 grid
successes remain successful, and 35 additional requests succeed because the
navmesh avoids conservative whole-cell rejection of otherwise valid endpoints.
The two methods therefore use the same source obstacles but different free-space
representations; the navmesh is not constrained to the raster's allowed cells.

Among the 943 shared successes, the median navmesh world-space polyline is
3.4652% longer, the largest increase is 12.2355%, and the median increase in
distance is 194.19 m. This compares **navmesh weight 8 against grid weight 2**,
including their different corridor and smoothing methods. It is not a measure
of stretch relative to an exact shortest path.

The selected 1,000-request batch stores 3,334,763 triangles in total and makes
2,424,729 expansions, 7,271,253 neighbor checks and 3,472,580 funnel portal
tests. These totals sum independent searches. The largest request stores
**8,228 triangles** and expands **7,115**, well below the 50,000-record cap.
The peak open queue has 1,114 entries. The resulting **146,139 waypoints** use
**2,338,224 bytes (23.38%)** of the path arena.

Weighted priorities can make reopening very expensive: the original weight-2
reference stores 10,862,427 triangles but makes 75,336,718 expansions and
226,007,220 neighbor checks across 1,000 requests. The optional
`--reopen-closed` control retains that behavior. The selected approximate mode
avoids this work by finalizing a triangle at its first expansion. Neither this
policy nor the funnel compromises the required collision checks.

## Runtime memory

MB below means 1,000,000 bytes. These are accounted navigation allocations,
excluding process overhead, replay JSON, aerial raster and benchmark validators.

| Allocation | Bytes | MB |
| --- | ---: | ---: |
| Shared vertices and triangles | 35,324,408 | 35.324 |
| Shared BVH and triangle order | 10,313,660 | 10.314 |
| One preallocated search workspace | 4,373,120 | 4.373 |
| Persistent waypoint arena | 10,000,000 | 10.000 |
| Manager metadata, 1,000 paths | 68,920 | 0.069 |
| **Total** | **60,080,108** | **60.080** |

This is about **1.80×** the grid experiment's 33,290,616-byte navigation
allocation. Shared immutable geometry and the reused search workspace do not
multiply by zombie count. Python/GEOS bake memory is only needed offline.
Peak selected benchmark RSS was 161,092 KiB; the grid control used 130,892 KiB.

## Verification

All four focused CTest suites pass, including 11 Python bake cases. They cover
binary rejection, BVH and independent segment queries, rotated obstacles,
clearance, fractional map boundaries, tile T junctions, tiny double-precision
geometry, atomic publication, Dijkstra graph-cost comparisons, funnel corners
in both directions, search limits, both reopening policies, reset, zero-allocation
queries, zombie mappings, replacement, stale handles and arena reclamation.
Search tests also pass AddressSanitizer and UndefinedBehaviorSanitizer; leak
detection was disabled for the environment's process-inspection restriction.

Every successful route preserves both endpoints and passes mesh segment
coverage after every benchmark pass. On the last pass, every segment is also
checked with a separate slab-intersection routine against the physical padded
source OBBs, excluding the bake's extra numerical margin. All paths pass.
The manager verifies mappings and reclaims the complete arena after each batch.
Additional actual-map checks locate every triangle centroid and validate all
30,000 edges of the 10,000 smallest triangles. All four aerial PNGs and the
contact sheet were visually inspected.

## Reproduce and artifacts

Run from `project-gdll-navmesh`. Reuse the existing Python environment with
NumPy, Shapely 2.1+ and Pillow; this bake used Shapely 2.1.1 / GEOS 3.13.1.
The runtime is C++17 and does not depend on Python or a graphics context.

```bash
# These paths match the existing environment and sibling dependency cache.
navigation_python="$PWD/../../project-gdll/.venv/bin/python"
dependency_cache="$PWD/../project-gdll/build/cached-deps"
cmake -S . -B build -DGD_VIEWER=OFF -DCMAKE_BUILD_TYPE=Release \
    -DPython3_EXECUTABLE="$navigation_python" \
    -DFETCHCONTENT_FULLY_DISCONNECTED=ON \
    -DFETCHCONTENT_SOURCE_DIR_GLM="$dependency_cache/glm-src" \
    -DFETCHCONTENT_SOURCE_DIR_TINYGLTF="$dependency_cache/tinygltf-src" \
    -DFETCHCONTENT_SOURCE_DIR_BOX3D="$dependency_cache/box3d-src" \
    -DFETCHCONTENT_SOURCE_DIR_MESHOPTIMIZER="$dependency_cache/meshoptimizer-src" \
    -DFETCHCONTENT_SOURCE_DIR_TOMLPLUSPLUS="$dependency_cache/tomlplusplus-src"
cmake --build build --target benchmark_zombie_navmesh \
    test_zombie_navmesh test_zombie_navmesh_search test_zombie_navmesh_paths -j4
ctest --test-dir build -R '^viewer_zombie_navmesh' --output-on-failure

replay=../project-gdll/build/zombie-pathfinding-relative-offsets/paths.json
"$navigation_python" tools/bake_zombie_navmesh.py --scene "$replay" \
    --output build/navmesh/world.navmesh --tile-size 256
taskset -c 2 ./build/benchmark_zombie_navmesh \
    --navmesh build/navmesh/world.navmesh --replay "$replay" \
    --heuristic-weight 8 --batches 1,10,100,1000 --repeats 5 \
    --output build/navmesh/weighted-8
"$navigation_python" tools/render_zombie_navmesh_paths.py build/navmesh/weighted-8/paths.json

# Fresh control, using the existing grid experiment's binary and baked grid.
taskset -c 2 ../project-gdll-grid-jps/build/benchmark_zombie_grid \
    --grid ../project-gdll-grid-jps/build/grid-jps/world.grid --replay "$replay" \
    --heuristic-weight 2 --batches 1000 --repeats 5 \
    --output build/navmesh/grid-control
```

The replay and raster come from the earlier continuous experiment; the town
is not regenerated. The benchmark rejects a different replay SHA-256.
This checkout was configured offline with the sibling build's cached dependency
sources and the existing Python environment. For reference reopening, append
`--reopen-closed`; for the other priorities, change `--heuristic-weight`.

- [Selected measurements](zombie-navmesh-weighted-8-measurements.csv),
  [weight-2 comparison](zombie-navmesh-weighted-2-measurements.csv),
  [weight-4 probe](zombie-navmesh-weighted-4-measurements.csv),
  [reopening reference](zombie-navmesh-reopen-reference-measurements.csv),
  [fresh grid control](zombie-navmesh-grid-control-measurements.csv).
- [1,000-zombie aerial view](../../gdll3/project-gdll-navmesh/build/navmesh/weighted-8/zombie_paths_1000.png),
  [all four aerial views](../../gdll3/project-gdll-navmesh/build/navmesh/weighted-8/zombie_paths_all.png).
- [Selected paths and metadata](../../gdll3/project-gdll-navmesh/build/navmesh/weighted-8/paths.json),
  [bake metadata](../../gdll3/project-gdll-navmesh/build/navmesh/world.navmesh.json),
  [comparison data](../../gdll3/project-gdll-navmesh/build/navmesh/comparison.json).

Large generated mesh, JSON and images remain in `build/`; this report and the
compact measurement CSVs record the measured results for review.
