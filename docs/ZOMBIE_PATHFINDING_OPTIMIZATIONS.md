# Navmesh query optimizations 1–3

The same 1,000 independent tree-free requests now take **127.528 ms** at the
median, compared with **186.772 ms** for a fresh run of the committed baseline.
The combined change reduces median time by **31.72%** (**1.465× faster**) while
preserving all route records and collision checks. It uses **4.760 MB** more
navigation memory. Individual contributions were not timed separately.

## Changes

1. **Reuse the goal cost in default searches.** Without reopening, every parent
   was already expanded and remains fixed, so the goal record's `g` is the
   reconstructed graph cost. The funnel no longer recalculates that diagnostic
   cost across the entire corridor. Reopening reference searches retain their
   full reconstructed-corridor cost calculation.
2. **Precompute triangle centers and neighbor distances.** The immutable mesh
   caches one double X/Z center and three double centroid-to-centroid edge costs
   per triangle at construction/loading. Cached values preserve the previous
   arithmetic exactly. BVH construction also reads these cached centers. The
   binary format and offline bake are unchanged; existing meshes still load.
3. **Index triangle IDs directly.** The preallocated lookup now stores a record
   index and generation for each triangle, removing hash mixing and probes.
   Generation resets, heap ordering and neighbor order are preserved. Records,
   queue, corridor and output remain bounded by the 50,000-record request cap.

Priority remains `g + 8h`, with no reopening for the selected variant. Each
zombie still searches independently. Polygon merging was not implemented.

## Matched benchmark

Baseline is port commit `6dc4588`, copied to a frozen executable before edits.
Both builds use GCC 14.2, Release `-O3 -DNDEBUG`, on the AMD Ryzen 5 7535HS.
The runner executes four processes serially on CPU 2, in this order:
baseline, optimized, optimized, baseline. Every process has one warm-up followed
by five measured 1,000-request passes: **ten measured crowd passes per variant**.
No samples were excluded. This is a comparison of the combined changes.

Inputs are the same 131,933-triangle mesh and 24,229 building/fence rectangles
from the earlier tree-free replay. All 147,495 tree trunks remain excluded.
Starts and relative-offset destinations are unchanged, with no endpoint snapping.
The scene SHA-256 is
`a0693058d0172c7d434b96a5a2fed51e13c6b495a4f62df1dc90ddc6a5f10fbe`;
the unchanged mesh SHA-256 is
`de72579c4e2ef44c8acc1f847e720d4fa1442f0dd7c84076ee8e6abe55e43c13`.

| Variant | Median per 1,000 requests | Min–max | Found | Blocked | Search limit |
| --- | ---: | ---: | ---: | ---: | ---: |
| Baseline | 186.772 ms | 172.547–230.325 ms | 999 | 1 | 0 |
| Optimized 1–3 | **127.528 ms** | 120.569–257.010 ms | 999 | 1 | 0 |

Per-process medians in execution order were 191.896, 139.929, 122.593 and
175.245 ms. The optimized maximum includes a 257.010 ms measured pass; it was
retained. CPU affinity does not guarantee exclusive CPU availability. Whole
process CPU/wall records again show approximately 50% effective CPU utilization;
its cause was not diagnosed. These results describe this fresh comparison,
without normalizing it against the historical 162.026 or 193.808 ms runs.

Timing includes synchronous search/reset, funnel, path allocation, zombie-ID
mapping, and status/stat collection. Loading, initial workspace allocation,
batch clearing, validation, JSON and images remain outside request timing.
Whole processes took 7.70–8.59 s because each also loads data and validates all
successful paths. Mesh load/BVH/cache time was 127.63–138.89 ms for baseline and
87.69–105.89 ms for optimized across two processes each. The new cache is filled
during loading; no extra bake or startup pass is required.

## Memory

Decimal MB; figures count allocated navigation buffers. Both variants keep all
999 successful paths live in the full **10,000,000-byte** arena during each pass.
One reusable workspace serves serial requests; it is not allocated per zombie.

| Buffer | Baseline bytes | Optimized bytes | Change |
| --- | ---: | ---: | ---: |
| Mesh vertices and triangles | 4,979,608 | 4,979,608 | 0 |
| Spatial index | 2,100,548 | 2,100,548 | 0 |
| Shared graph geometry cache | 0 | 5,277,320 | +5,277,320 |
| Search workspace | 4,373,120 | 3,855,720 | −517,400 |
| Path arena capacity | 10,000,000 | 10,000,000 | 0 |
| Manager metadata | 68,920 | 68,920 | 0 |
| **Total navigation payload** | **21,522,196** | **26,282,116** | **+4,759,920** |

The old lookup was 131,072 hash slots of 12 bytes; the new lookup is 131,933
direct slots of eight bytes. The shared cache costs 40 bytes per triangle.
Direct lookup savings depend on mesh size: the lookup spans every triangle,
even though a request may store only 50,000 records. A much larger mesh can use
more lookup memory than the previous bounded hash table. Separate simultaneous
search workspaces each need their own lookup; the geometry cache is shared.

Peak whole-process RSS was 47,236–47,240 KiB for baseline and 51,820–51,884 KiB
for optimized. RSS additionally includes replay parsing, raster, validators and
runtime overhead, and is distinct from the navigation allocation totals.

## Correctness and verification

All **1,000 complete route records** match the baseline exactly in all four
processes: waypoint coordinates/order, exact endpoints, path IDs, path lengths,
statuses, graph costs and per-request statistics. Aggregate counters also match:
696,737 stored nodes, 519,578 expansions, 379,265 corridor triangles,
1,555,737 neighbor tests, 566,858 funnel tests and 39,429 location tests.
There are still 38,629 waypoints occupying 618,064 arena bytes. The sole failure
is the same destination inside padded fence geometry; no search hits its limit.

Native checks still validate mesh coverage on every repetition and inflated
source OBBs on each process's final repetition. An independent Shapely/GEOS
closed-intersection check validates all **37,630 returned route segments with
zero collisions**, using 0.352 m building padding and 0.302 m fence padding,
with no tolerance or boundary shrinking. The additional 0.005 m bake numeric
margin, clearance settings, mesh coverage code and source-geometry collision
validator are unchanged. The prior aerial paths therefore still apply.

All four `viewer_zombie_navmesh*` CTest suites pass. Focused regressions cover
cached geometry on loaded/constructed meshes, irregular negative coordinates,
tiny triangles that collapse under float precision, known corridor costs in
both reopening modes, and a 50,004-triangle mesh with a four-record cap.
Alternating high/low triangle IDs, failures, resets and path-manager requests
remain allocation-free. Existing Dijkstra, funnel and collision checks remain.

## Saved evidence

- [All 20 measured samples](zombie-navmesh-optimizations-measurements.csv).
- [Comparison JSON](../build/navigation/optimizations-1-3/20261021T091820.694189Z/comparison.json),
  [source/binary/input manifest](../build/navigation/optimizations-1-3/20261021T091820.694189Z/manifest.json), and
  [exact source diff](../build/navigation/optimizations-1-3/20261021T091820.694189Z/optimized-source.diff).
- [Comparison runner](../build/navigation/optimizations-1-3/20261021T091820.694189Z/run_comparison.py), retained frozen executables,
  per-process logs/profiles, and complete paths in the same ignored run directory.
- [Optimized route records](../build/navigation/optimizations-1-3/20261021T091820.694189Z/03-optimized/paths.json) and
  [baseline route records](../build/navigation/optimizations-1-3/20261021T091820.694189Z/04-baseline/paths.json).

To reproduce manually, build as described in [navigation usage](ZOMBIE_PATHFINDING.md)
and run each preserved executable serially with identical `--navmesh`, `--replay`,
`--heuristic-weight 8 --batches 1000 --repeats 5` arguments, pinned to CPU 2.
Use separate new output directories. The manifest records the exact commands;
its source hashes cover the implementation and benchmark used for measurement.
