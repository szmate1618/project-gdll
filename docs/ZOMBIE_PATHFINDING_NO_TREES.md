# Zombie pathfinding comparison with trees ignored

> Historical matched experiment, preserved when navigation moved to this
> repository. See [the current navigation entry](ZOMBIE_PATHFINDING.md) for local
> build instructions. The timings below describe the original recorded run.

Navmesh was fastest on the same 1,000 independent, formation-preserving requests
with every tree trunk excluded: **162.026 ms** per crowd, versus **3,122.106 ms**
for continuous corner avoidance and **7,584.352 ms** for weighted grid JPS.
It found 999 paths; its only rejection was a destination inside a padded fence.
This compares the fastest previously tested variant of each method, without
retuning their algorithms for the new scene.

## Workload and selected variants

The recorded starts and destinations are unchanged from the relative-offset
experiment. Each destination preserves its start's offset within the crowd;
the common translation is approximately 5,520.682 m north. No blocked endpoint
was moved, snapped to free space, or replaced with another request.

Only obstacle kind `1` was removed from the shared replay: **147,495 trunks**.
The remaining **24,229 obstacles** comprise 9,981 building rectangles and 14,248
fence rectangles. Trees remain visible in the retained aerial background but
have no collision or traversal cost in any of these pathfinding runs.

| Method | Selected variant | Per-request search cap |
| --- | --- | --- |
| Continuous | Optimized greedy corner avoidance, fallback search and smoothing | 256 greedy steps; 4,096 fallback expansions; 256 local blockers |
| Grid | 0.5 m cells, JPS with `g + 2h`, normal reopening, early goal connections off | 50,000 distinct stored cells |
| Navmesh | Triangle search with `g + 8h`, single expansion per closed node, funnel | 50,000 distinct stored triangles |

All methods account for a 0.3 m zombie radius and 0.002 m skin, plus 0.05 m
extra padding for buildings. Grid cells touching those padded rectangles are
blocked in their entirety. Navmesh adds a separate 0.005 m numeric margin and
represents the continuous free-space boundaries using a tiled triangulation.
Roads do not introduce traversal weights or separate obstacles here.

## Matched measurements

Release GCC 14.2 (`-O3 -DNDEBUG`), AMD Ryzen 5 7535HS. The runner executes the
three binaries sequentially, pinned to CPU 2. Each
method receives one warm-up followed by five measured serial passes through
all 1,000 requests. All successful crowd paths remain live during each pass.
Search decisions are independent; neither results nor routes are shared among
zombies. Each manager retains its native allocator and search implementation.

Timing includes requests, search-state resets, smoothing/funnel processing,
path storage, zombie-ID mapping, and status/stat collection. Loading, baking,
batch clearing, validation, release, JSON output and images are excluded.
The continuous benchmark uses the new optional `--include-zombie-mapping` flag
to include its mapping insertion; its historical default scope is unchanged.
Hash-bucket reservation and mapping clearing remain outside that timer.

| Method | Median crowd time | Five-pass min–max | Found | Blocked endpoint | Search limit |
| --- | ---: | ---: | ---: | ---: | ---: |
| Continuous | 3,122.106 ms | 3,030.155–3,146.812 ms | 998 | 1 | 1 |
| Grid, weight 2 | 7,584.352 ms | 7,550.921–7,646.893 ms | 998 | 2 | 0 |
| Navmesh, weight 8 | **162.026 ms** | 158.562–166.432 ms | **999** | 1 | 0 |

Navmesh was **19.269× faster than continuous** and **46.809× faster than grid**
in these matched runs. Its average request cost was 162.026 microseconds,
including the rejected destination. No method returned `invalid_input`,
`no_path`, or `buffer_full`.

Whole benchmark processes took 18.92 s, 46.14 s and 6.99 s respectively,
including warm-up, five passes, loading, repeated native validation and output.
Their recorded user CPU times were 9.43 s, 23.02 s and 3.46 s. CPU affinity does
not establish exclusive CPU availability; the cause of the roughly 50% CPU
utilization was not diagnosed. These elapsed measurements should therefore be
compared within this run, without inferring speedups from older wall times.

## Failure and path checks

The three exceptional requests below use **one-based request numbers** in the
common input order. Continuous JSON uses zero-based `zombie_id` values;
grid/navmesh JSON uses one-based IDs.

| Zombie | Continuous | Grid | Navmesh | Explanation |
| ---: | --- | --- | --- | --- |
| 232 | Blocked endpoint | Blocked endpoint | Blocked endpoint | Goal intersects the body-expanded geometry of two fence panels. |
| 562 | Found | Blocked endpoint | Found | Physical goal is free; whole-cell rejection makes its grid cell blocked. |
| 871 | Search limit | Found | Found | Continuous fallback reached its 4,096-expansion cap; valid grid/navmesh paths demonstrate reachability. |

Every method passed its native endpoint and segment checks on every repetition.
The runner also independently checked every final successful path against the
same 24,229 padded source rectangles using Shapely/GEOS segment intersections:
**104,354 segments checked, zero collisions**. This uses the physical 0.352 m
building and 0.302 m fence padding, without shrinking either obstacle boundary
or applying a collision tolerance.

All 1,000 starts and goals match the recorded input as runtime float32 values.
Continuous JSON uses nine significant digits; grid JSON preserves its promoted
float values and navmesh prints double coordinates. Different text precision
does not mean different endpoints. Continuous/grid waypoints are restored to
their actual float32 values before the independent geometry checks. Navmesh
retains its double waypoint values.

On the **997 requests found by all three methods**, navmesh's route length was
1.326% higher at the median and at most 13.688% higher than grid's route for the
same request. Continuous had a median change of 0% and a maximum increase of
7.856%. Grid is a comparison reference, not a proven exact shortest-path oracle;
weighted search and the navmesh centroid graph do not guarantee globally
shortest continuous routes.

## Storage and search work

All methods retain a **10,000,000-byte waypoint arena**. Payload figures below
include the full arena capacity, rather than just its occupied portion.
MB means decimal megabytes; RSS is the whole benchmark's peak resident memory.

| Method | Navigation payload | Live waypoints | Used arena | Peak process RSS |
| --- | ---: | ---: | ---: | ---: |
| Continuous | Total unexposed; arena alone 10 MB | 22,172 | 177,376 B | 24,064 KiB |
| Grid | 33.291 MB | 46,548 | 372,384 B | 61,712 KiB |
| Navmesh | 21.522 MB | 38,629 | 618,064 B | 47,236 KiB |

Grid payload is 18,000,000 B occupancy, 5,221,736 B search workspace,
68,936 B manager metadata and the arena. Navmesh payload is 4,979,608 B mesh,
2,100,548 B spatial index, 4,373,120 B search workspace, 68,920 B metadata and
the arena. Continuous does not expose its index/workspace/metadata capacities;
its arena size must not be presented as its total navigation memory. RSS also
includes benchmark parsing, source-geometry validation and runtime overhead,
so it is a separate measurement rather than an allocation total.

Removing trunks made the navmesh substantially smaller: 113,326 vertices and
131,933 triangles in 343 connected components. Its file is **4,979,736 B**,
including the header. Grid remains 12,000 × 12,000 half-meter cells with an
18 MB bit-packed occupancy payload; 11,660,768 cells are blocked. Offline bake
times reported by the generators were 5.42 s for grid and 5.12 s for navmesh.
Grid loading took 17.94 ms; navmesh loading and BVH construction took 115.48 ms.
Their manager construction took a further 14.52 ms and 12.34 ms respectively;
continuous manager/obstacle-index setup took 14.69 ms. These costs are outside
request timing.

Grid stored 610,615 cells and expanded 535,980 across the final crowd pass,
yet performed **3,196,248,794 logical JPS cell scans**, plus 222,858,281 smoothing
probes. The earlier tree-inclusive weight-2 run reported 1,053,681,320 scans.
Long open rays scan farther when trunks disappear, so fewer stored states do
not necessarily make this JPS implementation cheaper. This counter comparison
explains the added scan work; it is not an older-versus-current wall-time ratio.
Navmesh expanded 519,578 nodes and tested 1,555,737 neighbors without those long
grid scans. Continuous performed 36,809,283 exact rectangle tests.

## Saved evidence and reproduction

The algorithms were committed before testing: continuous `49e5784`, grid
`2248718`, navmesh `338a293`. Continuous benchmark-scope commit `fbab320` and
comparison-runner commit `464e61d` add bookkeeping and comparison tools without
changing the continuous search algorithm. The run manifest records clean
tracked sources, commit IDs, copied binary hashes, input/cache hashes,
commands, CPU affinity and timing profiles.

The original run was `build/zombie-no-trees-comparison/20261021T004219.095809Z`
in the archived continuous experiment. Its retained evidence is now also copied
to `build/navigation/reference/no-trees/` in this repository.
Its [input manifest](../build/navigation/reference/no-trees/input-manifest.json)
records the original scene SHA-256
`886833659e1b1f12488eafb1830240ebb6501c43814dc447ecd8474bf0a90047` and filtered
scene SHA-256 `a0693058d0172c7d434b96a5a2fed51e13c6b495a4f62df1dc90ddc6a5f10fbe`.
The [measurement CSV](zombie-pathfinding-no-trees-measurements.csv),
[comparison JSON](../build/navigation/reference/no-trees/results/comparison.json)
and [run manifest](../build/navigation/reference/no-trees/results/manifest.json)
retain the results. Large scenes, caches, binaries and images remain build artifacts.

The prepared tree-free replay and matching navmesh are available locally under
`build/navigation/fixtures/no-trees/`. To rerun this navmesh variant after the
[headless build](ZOMBIE_PATHFINDING.md#build-verify-and-run):

```bash
taskset -c 2 ./build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/fixtures/no-trees/cache/world.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --heuristic-weight 8 --batches 1000 --repeats 5 \
    --output build/navigation/results/no-trees
.venv/bin/python tools/render_zombie_navmesh_paths.py \
    build/navigation/results/no-trees/paths.json
```

The local baker can rebuild the cache without regenerating the town:

```bash
.venv/bin/python tools/bake_zombie_navmesh.py \
    --scene build/navigation/fixtures/no-trees/scene/paths.json \
    --output build/navigation/results/rebake/world.navmesh
```

The full three-method runner remains in the
[archived experiment](../../gdll3/project-gdll/tools/benchmark_zombie_no_trees.py),
whose [original report](../../gdll3/project-gdll/docs/ZOMBIE_PATHFINDING_NO_TREES.md)
records the filtering and matched comparison commands. Its path from the current
repository root is `../gdll3/project-gdll/tools/benchmark_zombie_no_trees.py`;
it expects the original sibling worktrees and benchmark binaries. This port
includes only the navmesh implementation. Fresh scene serialization changes its
hash, so every cache must match its replay's exact bytes. No map or vegetation
regeneration is required. The original focused native CTest suites and four
comparison-runner unit tests passed in that experiment.

[Side-by-side aerial paths](../build/navigation/reference/no-trees/results/comparison_paths.png)
and individual [continuous](../build/navigation/reference/no-trees/results/continuous/visual/zombie_paths_1000.png),
[grid](../build/navigation/reference/no-trees/results/grid/visual/zombie_paths_1000.png)
and [navmesh](../build/navigation/reference/no-trees/results/navmesh/visual/zombie_paths_1000.png)
images show the unchanged aerial scene with each method's resulting paths.
