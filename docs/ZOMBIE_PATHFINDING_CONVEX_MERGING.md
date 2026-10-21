# Convex polygon merging investigation

**Four vertices is the default merge limit for the CLI and Python API.** It reduces median time for
1,000 independent tree-free requests from **110.839 ms to 55.847 ms**: **1.985×
faster, or 49.61% less time**. Navigation allocations fall from **26.282 MB to
23.039 MB**. Both versions find the same 999 paths; the remaining destination
is inside a padded fence. Exact route shapes change, while endpoints and
walkable geometry remain unchanged.

Six-vertex polygons have nearly the same measured speed and use less memory,
but produce worse route lengths. Their 55.654 ms median is only 0.34% below the
four-vertex median, with overlapping sample ranges. The measurements do not
establish a dependable speed advantage for either merge limit.

## Implementation

`tools/merge_zombie_navmesh.py` postprocesses the existing triangle cache. It
greedily merges adjacent polygons in descending shared-edge length, using
deterministic IDs and versioned heap entries to discard stale candidates.
A pair merges only if it shares exactly one reciprocal edge, its combined
boundary has at most the requested number of vertices, and every vertex lies
inside all counterclockwise edge half-planes. Convexity uses extended precision
without a concavity tolerance. Collinear vertices are retained to preserve
conforming portals. There is no coordinate welding, simplification, new
rasterization, clearance change or removal of obstacles.

The C++ mesh, spatial queries, cached graph geometry, weighted search and
funnel now support three through six edges. Polygon/edge buffers are compact
flat arrays; requests remain allocation-free. Each zombie searches independently
with `g + 8h`, no closed-node reopening and a **50,000 distinct stored polygon**
limit. The persistent path arena remains exactly **10,000,000 bytes**.

When retained collinear vertices split a shared opening into several edges,
the funnel uses the complete contiguous collinear span, including ring wrap.
A regression covers both directions, different ring starts and both search
modes. The first prototype lacked that case; the complete comparison below
was rerun after correcting it.

## Geometry validation and preprocessing

The input is the same 131,933-triangle, 113,326-vertex tree-free mesh. All original
double vertex bytes, map bounds, clearance settings and replay SHA are preserved.
The merger independently verifies that every original triangle is covered by
its assigned output polygon using GEOS; memberships cover the complete input.
The directed boundary-edge set is identical, free-space area is unchanged,
and connected components remain identical. Validated meshes are published atomically.

| Mesh | Regions | Directed edges | Triangle / quad / pentagon / hexagon counts | Extra offline processing |
| --- | ---: | ---: | --- | ---: |
| Original | 131,933 | 395,799 | 131,933 / 0 / 0 / 0 | Already baked |
| Max 4 vertices | 80,277 | 292,487 | 28,621 / 51,656 / 0 / 0 | 13.01 s |
| Max 6 vertices | 60,016 | 251,965 | 18,273 / 22,046 / 9,220 / 10,477 | 16.06 s |

Both merged meshes retain all **112,847 boundary edges**, **343 components**, and
**33,329,851.44570496 m²** of free space. Postprocessing times include reading,
merging and validation; they are separate from query timing and the earlier
triangle bake. Cache files are 4,474,348 B (max 4) and 4,069,128 B (max 6),
versus 4,979,736 B for the original triangle file.

## Matched performance

Baseline is the frozen committed executable from `3188642`. A control runs the
same original triangle cache through the generalized runtime. Its complete
1,000 route records and aggregate counters match baseline exactly after the
`corridor_triangles` → `corridor_polygons` statistic-name change. The control
exposes the overhead of generalized polygon storage/loops separately from merging.

Release GCC 14.2 (`-O3 -DNDEBUG`), Ryzen 5 7535HS. All requests are serial and
pinned to CPU 2. Eight processes run in order: baseline, control, max 4, max 6,
max 6, max 4, control, baseline. Each process has one warm-up plus five measured
1,000-request passes: **ten measured samples per variant**. No samples were
excluded. Each pass keeps all successful crowd paths live.

| Variant | Median crowd time | Ten-pass min–max | Navigation payload | Found / blocked / search limit |
| --- | ---: | ---: | ---: | --- |
| Committed triangle baseline | 110.839 ms | 108.412–114.852 ms | 26.282 MB | 999 / 1 / 0 |
| Generalized runtime, unmerged control | 123.250 ms | 120.060–127.526 ms | 27.338 MB | 999 / 1 / 0 |
| **Max 4 vertices — recommended** | **55.847 ms** | 50.705–59.258 ms | **23.039 MB** | **999 / 1 / 0** |
| Max 6 vertices | 55.654 ms | 51.885–65.298 ms | 21.268 MB | 999 / 1 / 0 |

Timing includes independent requests, resets, funnel, path storage, zombie-ID
mapping and result statistics. Initial loading/allocation, batch clearing,
validation, JSON and images are excluded. Process profiles again show roughly
50% CPU time relative to elapsed time, of undiagnosed cause. This fresh baseline
is the valid comparison; the earlier 127.528 ms measurement is historical.

| Work over 1,000 requests | Baseline | Max 4 | Max 6 |
| --- | ---: | ---: | ---: |
| Stored nodes | 696,737 | 403,980 | 403,053 |
| Expanded nodes | 519,578 | 223,834 | 224,229 |
| Neighbor tests | 1,555,737 | 841,510 | 1,002,798 |
| Funnel tests | 566,858 | 315,307 | 260,896 |
| Point-location tests | 39,429 | 22,744 | 33,904 |
| Median successful corridor regions | 410 | 227 | 166 |

Six vertices reduce region/corridor counts further, but here they do not reduce
expansions below the four-vertex version and require more neighbor and location
tests. Fewer polygons alone do not guarantee faster requests.

## Path quality and collision checks

All 999 successful routes change for each merged mesh. Comparisons below use
the same request's baseline route length; the weighted centroid graph has no
exact shortest-path guarantee in any variant.

| Variant | Median length change | 95th percentile | Largest increase | Live waypoints |
| --- | ---: | ---: | ---: | ---: |
| Baseline/control | 0% | 0% | 0% | 38,629 |
| **Max 4** | **−0.581%** | +0.959% | **+1.089%** | **27,799** |
| Max 6 | +1.704% | +9.859% | +12.069% | 39,631 |

Four vertices provide essentially the same measured speed with substantially
better route lengths. Six vertices save a further 1.771 MB and remain available
if that memory tradeoff matters. The split-portal correction removes two extra
waypoints from the initial six-vertex prototype, so it does not explain that
variant's broad route-length increase; polygon grouping and centroid-graph
search decisions also change.

Endpoints match the recorded runtime float32 start/goal values exactly, promoted
to doubles. The sole rejection remains one-based request **232**, whose goal
overlaps body-expanded fence geometry. No endpoints were snapped or relocated.
All 147,495 tree trunks remain excluded; buildings and fences are unchanged.

Native mesh coverage checks run every repetition, with exact padded source OBB
checks on each process's final pass. Independent GEOS closed-intersection
checks validate **26,800 segments for max 4**, **38,632 for max 6**, and 37,630
each for baseline/control: **zero collisions**. Physical padding remains 0.352 m
for buildings and 0.302 m for fences; the bake retains its additional 0.005 m
numeric margin. No boundary shrinking or collision tolerance was introduced.

## Memory, format and tests

| Buffer | Committed triangles | Max 4 | Max 6 |
| --- | ---: | ---: | ---: |
| Mesh payload | 4,979,608 B | 4,795,328 B | 4,309,064 B |
| Geometry cache | 5,277,320 B | 3,624,328 B | 2,975,976 B |
| Spatial index | 2,100,548 B | 1,107,492 B | 633,232 B |
| Search workspace | 3,855,720 B | 3,442,472 B | 3,280,384 B |
| Path arena capacity | 10,000,000 B | 10,000,000 B | 10,000,000 B |
| Manager metadata | 68,920 B | 68,920 B | 68,920 B |
| **Total** | **26,282,116 B** | **23,038,540 B** | **21,267,576 B** |

The control adds eight bytes of polygon offset/count metadata per original
triangle. In merged meshes, fewer regions, edges, cache entries and lookup slots
more than offset that overhead. Original vertex payload is retained even if
some vertices become unused. Peak process RSS was 51,884 KiB baseline,
53,292–53,308 KiB control, 48,716–48,772 KiB max 4 and 48,324–48,384 KiB max 6.
RSS includes parsing, raster and validation beyond the navigation buffers.

The loader accepts legacy **GDNAV01** and new **GDNAV02**. Both use the same
128-byte header and double vertex payload. V2 records contain a vertex count,
that many vertex IDs, and that many neighbor IDs, all little-endian uint32.
The in-memory API now exposes `Mesh::polygons()` and `Mesh::edges()`;
`Triangle` remains a legacy construction recipe. Search stats use
`corridorPolygons` / JSON `corridor_polygons`. The triangle recipe and v1 loading
preserve original centroid arithmetic and search order.

All **five** navigation CTest suites pass, including the eight new merger tests.
Regressions cover malformed v2 payloads, non-convex/self-crossing polygons,
holes and disconnected components, collinear seams, tiny double geometry,
fourth/fifth/sixth-edge traversal, funnel orientation, complete split portals,
Dijkstra graph costs, request caps and allocation-free resets/path management.

## Reproduce and saved artifacts

From the repository root, after the [headless build](ZOMBIE_PATHFINDING.md#build-verify-and-run):

```bash
.venv/bin/python tools/merge_zombie_navmesh.py \
    --input build/navigation/fixtures/no-trees/cache/world.navmesh \
    --output build/navigation/results/merged-4.navmesh --max-vertices 4
taskset -c 2 ./build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/results/merged-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --heuristic-weight 8 --batches 1000 --repeats 5 \
    --output build/navigation/results/merged-4-paths
```

The CLI and Python API default to four vertices. Use `--max-vertices 6`
and separate output paths for the experimental alternative. The benchmark passed
each limit explicitly; choosing four as the default changes only calls that
omit the limit. The selected
cache is also retained locally as
`build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh`. Existing maps
and vegetation are not regenerated.

- [All 40 measured samples](zombie-navmesh-convex-merging-measurements.csv).
- [Comparison JSON](../build/navigation/convex-merging/20261021T095150.072228Z/comparison.json),
  [manifest and hashes](../build/navigation/convex-merging/20261021T095150.072228Z/manifest.json),
  [source snapshot](../build/navigation/convex-merging/20261021T095150.072228Z/source-snapshot/tools/merge_zombie_navmesh.py),
  [runner](../build/navigation/convex-merging/20261021T095150.072228Z/run_comparison.py).
- [Four-vertex cache metadata](../build/navigation/convex-merging/20261021T095150.072228Z/cache/merged-4.navmesh.json) and
  [six-vertex metadata](../build/navigation/convex-merging/20261021T095150.072228Z/cache/merged-6.navmesh.json), including complete
  original-triangle → output-polygon maps and unchanged boundary/component checks.
- [Baseline aerial paths](../build/navigation/convex-merging/20261021T095150.072228Z/08-triangles-baseline/visual/zombie_paths_1000.png),
  [recommended four-vertex paths](../build/navigation/convex-merging/20261021T095150.072228Z/06-merged-4/visual/zombie_paths_1000.png),
  [six-vertex paths](../build/navigation/convex-merging/20261021T095150.072228Z/05-merged-6/visual/zombie_paths_1000.png).

The manifest preserves frozen executables, exact commands, source/input/cache
hashes and per-process time/RSS profiles. The first prototype's safe but narrowed
split-portal results remain in a separate directory recorded by the manifest;
only the corrected full-portal run above supports the final comparison.

Aerial captions show the corresponding process's five-pass median; the table
above pools ten measured samples per variant. All three aerial images were
visually inspected.
