# Navmesh priority sweep and random destinations

**Weight 8 has the lowest measured formation median; weight 4 has the lowest
random-pair median.** Increasing the weight to 16/32 or using pure greedy does
not establish a dependable speed improvement. Sample ranges overlap substantially.
The random workload finds **960 of 1,000 paths with every setting**; all 40
failures connect different walkable components, confirmed independently afterward.

## Workloads and fixed inputs

Both workloads use the same tree-free four-vertex mesh: **80,277 polygons**,
113,326 vertices, 292,487 directed edges and 343 connected components on the
roughly 6 km × 6 km map. Buildings and fences retain their previous expanded
geometry. All 147,495 tree trunks remain ignored. The mesh is not rebaked.

- **Formation:** the previous 1,000 starts and relative-offset destinations,
  each displaced 5,520.682 m north. Float32 endpoint values are retained exactly,
  promoted to doubles. There are 999 connected pairs and one blocked destination.
- **Random:** 1,000 independent start/destination pairs sampled throughout map
  bounds, rejecting candidates that are not walkable after float32 rounding.
  There is no snapping, minimum-distance rule, direction preference, or component
  filtering. Seed **1515146562**, MT19937 with an explicit 53-bit coordinate mapping.
  The 2,000 accepted endpoints require 2,165 candidates (165 rejected): 1,086
  start attempts and 1,079 goal attempts. Straight-line distances range from
  **94.923 m to 7,516.552 m**, median **3,269.359 m**.

Every priority receives exactly the same pairs within each workload. Endpoint
pair SHA-256 hashes encode little-endian double X/Z starts and goals in order:

```text
formation: e56556e0a9bd7baac260a880e5564c83a7ac046e881606df966cde52093a0bfe
random:    e9de16c8e2678ddc8d0e2fc38dd1b4adba5f512dacbd831c4bb8a8b249df352e
```

The original geometry replay remains the provenance source. Its exact SHA is
verified against the navmesh before random generation, which occurs outside
timed requests. Changing the random seed does not require a geometry rebake.
All generated endpoints and sampling metadata are retained in each `paths.json`.

## Matched timing

GCC 14.2 Release (`-O3 -DNDEBUG`), Ryzen 5 7535HS, serial requests pinned to CPU 2.
Each setting/workload runs twice in opposite order. Each process has one warm-up
and five measured passes: **ten samples per table entry**. Committed weight-8
controls bracket the sweep, making 26 processes, 130 measured 1,000-request
passes and 26 warm-up passes. No samples are excluded.

| Priority | Formation median, ms | Formation min–max, ms | Random median, ms | Random min–max, ms |
| --- | ---: | ---: | ---: | ---: |
| `g + 2h` | 86.692 | 82.342–99.739 | 540.341 | 519.315–660.308 |
| `g + 4h` | 64.939 | 56.789–80.901 | 481.114 | 461.660–640.549 |
| `g + 8h` | 61.150 | 53.640–64.298 | 526.379 | 503.847–611.735 |
| `g + 16h` | 61.633 | 57.949–72.141 | 555.240 | 462.558–607.609 |
| `g + 32h` | 63.391 | 59.346–74.973 | 537.598 | 481.192–625.870 |
| Pure greedy `h` | 62.486 | 57.783–76.467 | 491.851 | 466.632–552.892 |

Weight 4's random median is **8.60% below weight 8** in this run, while
their ranges overlap. Formation weights 8 and 16 are within 1% by median.
The data support retaining weight 8 for the formation case and considering
weight 4 for varied destinations; they do **not** establish weight 8 as a
universal winner or suggest that a still greedier priority reliably helps.

The frozen executable from `531729d` has a fresh weight-8 formation median of
**60.416 ms**. Candidate weight-8 routes,
endpoints, statuses, counters and waypoint bytes match it exactly. Earlier
55.847 ms merging measurements are historical, not the matched control here.
Whole-process CPU time is again approximately half elapsed time; its cause is
unresolved. Full ranges and both process orders are retained, without CPU-ratio
normalization or a claim of statistically established small speed differences.

Timing includes each independent search/reset, funnel, 10 MB path-arena storage,
zombie-ID mapping and result-stat collection. It excludes loading, initial
allocation, random generation, batch clearing, collision checks, serialization
and images. All successful paths remain live throughout each crowd pass.

## Failures and search work

Every setting has identical status counts:

| Workload | Found | Blocked endpoint | Exhausted component (`no_path`) | 50,000 stored-polygon cap | Other failures |
| --- | ---: | ---: | ---: | ---: | ---: |
| Formation | 999 | 1 | 0 | 0 | 0 |
| Random | 960 | 0 | 20 | 20 | 0 |

An independent traversal of the mesh adjacency graph, followed by GEOS endpoint
location, confirms **960 connected random pairs and 40 disconnected pairs**.
All 960 connected requests succeed for every priority. The runtime cap alone
does not prove a destination unreachable; this classification is performed
afterward and never filters requests or accelerates timed searches.

At weight 8, the 20 capped requests store exactly **1,000,000 polygons** and
expand **996,891** between them. The 20 exhausted requests expand another
**2,718**. Thus failures account for **75.79% of all random expansions**, while
the 960 successful requests expand 319,272 polygons. This explains why the
random case is much slower despite shorter median trip distance.

| Priority | Formation expansions | Random expansions | Random stored polygons | Random live waypoint bytes |
| --- | ---: | ---: | ---: | ---: |
| `g + 2h` | 353,111 | 1,442,727 | 1,594,215 | 528,016 B |
| `g + 4h` | 226,044 | 1,333,887 | 1,487,069 | 556,192 B |
| `g + 8h` | 223,834 | 1,318,881 | 1,471,945 | 568,192 B |
| `g + 16h` | 226,028 | 1,315,556 | 1,468,028 | 578,128 B |
| `g + 32h` | 227,655 | 1,315,329 | 1,467,458 | 582,896 B |
| Pure greedy `h` | 228,306 | 1,315,337 | 1,467,776 | 587,920 B |

Larger heuristic weights do not guarantee fewer expansions: changing which
obstacle detours are explored can undo the expected benefit. In the random
case, weights 16/32 and pure greedy barely reduce work beyond weight 8 because
they still perform the expensive unsuccessful searches. A useful subsequent
optimization is an offline connected-component label checked before searching;
that would preserve independent routes while rejecting these impossible pairs
immediately. **That optimization is not applied in these timings.**

## Route lengths and validation

Length changes compare the same successful request against weight 2: 999 shared
formation successes and 960 random successes. Weight 2 is an approximate
reference, **not an exact shortest-path ground truth**. Lower centroid-graph
cost need not produce a shorter funnelled route.

| Priority | Formation length change: median / p95 / maximum | Random length change: median / p95 / maximum |
| --- | ---: | ---: |
| `g + 2h` | +0.000% / +0.000% / +0.000% | +0.000% / +0.000% / +0.000% |
| `g + 4h` | -0.260% / +0.287% / +1.027% | +0.262% / +5.242% / +36.685% |
| `g + 8h` | -0.424% / +0.701% / +2.094% | +0.635% / +6.709% / +36.685% |
| `g + 16h` | -0.458% / +0.821% / +7.528% | +1.054% / +7.869% / +36.685% |
| `g + 32h` | -0.425% / +0.910% / +7.528% | +1.261% / +9.194% / +36.685% |
| Pure greedy `h` | -0.364% / +0.910% / +9.432% | +1.445% / +9.662% / +36.685% |

Pure greedy has no measured speed advantage over weight 8 in the formation
case and has a larger worst formation length increase. For random destinations,
weight 4 combines the lowest observed median with less median/p95 route
stretch than the greedier settings; maximum increases remain substantial on
some requests even at weight 4.

Native validation checks exact endpoints and navmesh coverage every repetition,
and strict expanded source OBB intersections on each process's last repetition.
An independent GEOS pass checks **403,164 segments** across the 12 candidate
workload/priority combinations plus the committed formation control: **zero
obstacle intersections**. Physical padding is unchanged: 0.352 m buildings,
0.302 m fences; the mesh also retains its separate 0.005 m numeric margin.
No boundary shrinking or collision tolerance is introduced.

## Implementation, memory and checks

`Search` and `PathManager` accept an optional final `greedy` boolean, default
false. In that mode the priority is truly **`h` alone**, while accumulated `g`
and parent improvements are retained for reconstruction. Equal priorities keep
the existing larger-`g`, then smaller-polygon-ID tie order. Improving `g` at an
unchanged priority now repairs the indexed heap downward; weighted rounding
ties benefit from the same fix. Default searches still expand each polygon
at most once, with an optional reopening reference mode.

Navigation buffers remain **23,038,540 B (23.039 MB)** for every setting:
3,442,472 B search workspace, exactly 10,000,000 B path capacity, plus unchanged
shared mesh/cache/index and manager metadata. No per-request allocation is
introduced. The node cap remains **50,000 distinct stored polygons**, including
queued records, not only expanded records. Existing API defaults remain weight 2.

All **six focused CTest suites pass**: mesh, search, path manager, random inputs,
bake and merge. New regressions include an independent linear-frontier oracle
for greedy heap repairs, zero-allocation greedy requests/failures/resets,
manager ownership, seed-zero repeatability, request-prefix stability, rounded
walkability rejection and deliberately retained disconnected pairs. CLI checks
reject greedy combined with explicit weight, invalid seeds and a mismatched
geometry replay before random generation.

## Reproduction and artifacts

From this repository after the headless build described in
[the navigation guide](ZOMBIE_PATHFINDING.md):

```bash
# One random 1,000-request run; omit --random-seed to use the recorded formation.
build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --random-seed 1515146562 --heuristic-weight 4 \
    --batches 1000 --repeats 5 --output build/navigation/random-weight4
# Use --greedy instead of --heuristic-weight for pure greedy.

# Full six-priority comparison, forward/reverse, in a fresh directory.
.venv/bin/python tools/benchmark_zombie_navmesh_weights.py \
    --binary build/navmesh-navigation/benchmark_zombie_navmesh \
    --navmesh build/navigation/fixtures/no-trees/cache/world-convex-4.navmesh \
    --replay build/navigation/fixtures/no-trees/scene/paths.json \
    --output build/navigation/weight-sweep/new-run
```

The complete run is `build/navigation/weight-sweep/20261021T103658.695092Z`. It retains frozen executables, source
snapshots/diff/hashes, scene/mesh hashes, commands, profiles, every request and
sample, endpoint hashes, connectivity classifications and independent geometry
results in `manifest.json`, `processes.json` and `comparison.json`.
The optional harness `--baseline PATH` adds the committed controls; the measured
run used the executable frozen before the edits.

- [All 130 measured samples](zombie-navmesh-weight-sweep-measurements.csv).
- [Formation, weight 8 aerial paths](../build/navigation/weight-sweep/20261021T103658.695092Z/02-formation-weight-8/zombie_paths_1000.png).
- [Random pairs, weight 4 aerial paths](../build/navigation/weight-sweep/20261021T103658.695092Z/07-random-weight-4/zombie_paths_1000.png).
- [Random pairs, pure greedy aerial paths](../build/navigation/weight-sweep/20261021T103658.695092Z/13-random-greedy/zombie_paths_1000.png).

Image headers show the five-pass median for that particular process; the tables
above pool both processes into ten samples. Route geometry is identical between
repetitions for each setting.

Replay SHA-256: `a0693058d0172c7d434b96a5a2fed51e13c6b495a4f62df1dc90ddc6a5f10fbe`.
Mesh SHA-256: `644ba9efb20de75091e1ddd059f3e258e45b13348320683359dbee16d30be200`.
Generated inputs, rasters, paths and caches remain ignored build artifacts.
