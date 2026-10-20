# Frustum visibility benchmark

`benchmark_visibility` compares fixed frustum-plane order with temporal rejecting
plane hints in the production `VisibilityIndex` BVH. It runs without a display,
using four deterministic synthetic scenes and, optionally, the existing map's
transformed draw bounds through `ModelVisibility`.

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target benchmark_visibility -j4
mkdir -p build/benchmarks/visibility
./build/benchmark_visibility --model output/godollo.glb \
  > build/benchmarks/visibility/unlimited.csv
# Also exercise the current branch's horizontal distance limit:
./build/benchmark_visibility --model output/godollo.glb --range 100 \
  > build/benchmarks/visibility/range-100m.csv
# Longer samples extend the smooth camera path and change that workload:
./build/benchmark_visibility --frames 600 --repeats 15
```

Times are CPU microseconds per query. They include frustum extraction, traversal,
plane-hint bookkeeping, result writing and statistics accumulation. They exclude
loading, BVH construction, scene/camera generation, correctness checks, rendering,
GPU work and presentation. Use an optimized build on an otherwise idle machine.

## Temporal heuristic

Each view owns its own `VisibilityQueryState`. It remembers one rejecting plane
per BVH node and source bound. A query tests the remembered plane first against
the current frustum and, if necessary, tests every remaining plane. Only the
plane order is reused; no previous acceptance or rejection substitutes for a
current geometry test. Visible indices and traversal order stay identical.

The renderer keeps independent camera, sun-shadow and flashlight-shadow states,
including separate forest query states. Concurrent callers also need separate
states. Storage is approximately one byte per source bound and BVH node per
view, plus the vector and previous-plane snapshot overhead. The scene's geometry
and indexes remain immutable and shared.

If any normalized frustum plane rotates more than 10 degrees or its offset
changes more than 25 meters since that state's preceding query, the query uses
fixed plane order. The snapshot still advances, so hints become available again
on the next coherent frame. The gate limits predictor overhead after camera
cuts, teleports and large projection changes. It is a performance heuristic;
correctness comes from testing against the current frustum in every query.

The branch's horizontal visibility range, optional frustum disablement, appended
forest-group results, building-detail filtering and motion-padded model bounds
retain their existing behavior. Range checks remain independent of plane order.

## Fixtures and camera motion

Synthetic bounds use runtime world meters with Y up. Fixed Mersenne Twister seeds
and explicit integer-to-float conversion keep their generation reproducible.

| Scene | Bounds | Distribution |
| --- | ---: | --- |
| `town_grid` | 25,600 | Buildings on a 14 m street grid, heights 4–32 m |
| `forest_clusters` | 65,536 | Trees in 32 irregular forest patches, heights 5–28 m |
| `road_corridor` | 24,000 | Roadside objects along a narrow 6 km corridor |
| `sparse_mixed` | 16,000 | Sparse small props mixed with warehouse/terrain-sized bounds |

Each scene runs three camera sequences with a 900 m far plane:

- `stationary`: the same 60-degree perspective camera every frame.
- `smooth`: walking at 5 m/s, gentle head motion and about 7 degrees/s turning,
  sampled at 60 Hz.
- `teleport`: independently randomized position, direction, field of view and
  aspect ratio every frame, deliberately reducing coherence.

The default sequence contains 240 frames and uses nine timed repetitions.
`--model PATH` adds three `model` rows using the production loader and
`ModelVisibility`; the map is neither regenerated nor modified. These cameras
are relative to the map bounds' center, at the vertical midpoint plus 1.8 m for
stationary/smooth motion. They can be aerial or cross geometry; there is no
collision simulation or distance-based building-detail filtering in this
isolated culling measurement.

`--range METERS` repeats these queries with a horizontal circle centered on each
frame's camera position, testing the range-aware BVH path added on this branch.
The CSV's `range_m` is zero for unlimited queries. This applies the limit to all
fixture bounds; the viewer selectively applies its old-forest limit according
to the camera's forest membership.

## Measurement and verification

Before timing each case, both variants run for two passes through every frame,
including the last-to-first sequence wrap. The benchmark requires exact ordered
visible-index equality and identical node/instance visit counts. A mismatch
fails the run.

A two-second startup warmup exercises both variants before the first case.
Each timed pass then creates its own state/result vector, reserves result
capacity and runs one untimed sequence. Initial allocations and cache population
are excluded. Variant order alternates across repetitions; reported times are
medians. This measures warmed queries, including their ordinary per-query hint
validation, rather than first-use allocation latency.

`baseline_planes`, `temporal_planes`, `temporal_rejects` and `visible` are mean
counts per query. `plane_reduction_pct` is the decrease in plane tests;
`speedup` is fixed time divided by temporal time. Values greater than one favor
hints. `temporal_rejects` counts rejection by the preferred first plane,
including an initially default hint; it does not mean a geometry test was
skipped.

The fixed-order baseline includes the added plane counters and optional-state
dispatch. It isolates plane ordering within the new implementation. A direct
comparison with the branch before this change is needed to measure net CPU
render benefit, including that bookkeeping overhead. GPU time requires a real
renderer benchmark with asynchronous timestamps; CPU microseconds here do not
predict whole-frame FPS. CPU and GPU work overlap and their timings must not be
added together.

## Measured CPU results on `implement-muzzle-flash`

These measurements were rerun on 2026-10-20 after integrating plane hints into
`implement-muzzle-flash`, whose pre-change HEAD was `239261fcb467`.
Hardware: AMD Ryzen 5 7535HS, six cores/twelve threads, boost enabled;
GCC 14.2.0, C++17, Release (`-O3 -DNDEBUG`), with the benchmark pinned to CPU 2.
Both runs used 240 frames, nine repetitions and the warmup/median methodology
above, sequentially without concurrent benchmark or build workloads.

The unchanged map contained 49,431 draw bounds from the existing 178,755,076-byte
GLB, SHA-256
`a98d8bd007c07260533749aefff16cb00de54cf104dd4cf38438dcc4bf3992c5`.
All ordered-output and traversal comparisons passed: 14,400 frame pairs across
30 cases, including sequence wraps. These are new branch measurements;
previous branch timings are not reused.

### Unlimited horizontal range

| Scene | Motion | Fixed µs | Hints µs | Speedup | Fewer plane tests |
| --- | --- | ---: | ---: | ---: | ---: |
| `town_grid` | stationary | 35.658 | 31.792 | 1.122× | 18.8% |
| `town_grid` | smooth | 38.777 | 35.034 | 1.107× | 19.0% |
| `town_grid` | teleport | 27.881 | 27.695 | 1.007× | 0.0% |
| `forest_clusters` | stationary | 88.699 | 83.543 | 1.062× | 14.0% |
| `forest_clusters` | smooth | 96.320 | 90.110 | 1.069× | 17.5% |
| `forest_clusters` | teleport | 53.126 | 52.634 | 1.009× | 0.0% |
| `road_corridor` | stationary | 11.319 | 10.147 | 1.115× | 19.5% |
| `road_corridor` | smooth | 14.369 | 13.224 | 1.087× | 17.8% |
| `road_corridor` | teleport | 14.783 | 14.978 | 0.987× | 0.0% |
| `sparse_mixed` | stationary | 16.800 | 14.839 | 1.132× | 22.5% |
| `sparse_mixed` | smooth | 18.155 | 15.287 | 1.188× | 27.1% |
| `sparse_mixed` | teleport | 17.900 | 18.245 | 0.981× | 0.0% |
| `model` | stationary | 25.739 | 21.321 | 1.207× | 26.5% |
| `model` | smooth | 25.013 | 20.431 | 1.224× | 27.4% |
| `model` | teleport | 23.629 | 23.713 | 0.996× | 0.0% |

Stationary/smooth cases had a geometric mean speedup of 1.130×
(11.5% lower query time), with 14.0–27.4% fewer plane tests.
Across all 15 cases the geometric mean speedup was 1.083×.
Teleport queries all used fixed order and performed identical plane counts;
the small timing differences do not indicate saved geometry work.

### Horizontal range of 100 meters

| Scene | Motion | Fixed µs | Hints µs | Speedup | Fewer plane tests |
| --- | --- | ---: | ---: | ---: | ---: |
| `town_grid` | stationary | 2.305 | 2.342 | 0.984× | 5.1% |
| `town_grid` | smooth | 2.205 | 2.266 | 0.973× | 4.1% |
| `town_grid` | teleport | 2.619 | 2.598 | 1.008× | 0.0% |
| `forest_clusters` | stationary | 11.144 | 11.478 | 0.971× | 4.1% |
| `forest_clusters` | smooth | 11.196 | 11.678 | 0.959× | 3.4% |
| `forest_clusters` | teleport | 5.517 | 5.466 | 1.009× | 0.0% |
| `road_corridor` | stationary | 4.734 | 4.934 | 0.960× | 5.6% |
| `road_corridor` | smooth | 4.679 | 4.944 | 0.946× | 3.9% |
| `road_corridor` | teleport | 4.368 | 4.446 | 0.983× | 0.0% |
| `sparse_mixed` | stationary | 0.819 | 0.849 | 0.964× | 5.4% |
| `sparse_mixed` | smooth | 0.837 | 0.867 | 0.966× | 5.5% |
| `sparse_mixed` | teleport | 1.161 | 1.174 | 0.989× | 0.0% |
| `model` | stationary | 3.080 | 3.228 | 0.954× | 4.4% |
| `model` | smooth | 3.276 | 3.337 | 0.982× | 6.8% |
| `model` | teleport | 2.663 | 2.659 | 1.002× | 0.0% |

The range rejects many bounds before their frustum tests, leaving less work
for plane hints to save. Stationary/smooth cases saved only 3.4–6.8% of plane
tests and had a geometric mean speedup of 0.966×
(3.5% more query time). All 15 range-limited cases
combined had a geometric mean speedup of 0.976×
(2.4% more query time). For the real map's smooth path,
the difference was 0.061 µs per query. Hints do not improve every workload;
these range-aware results are included in assessing the branch integration.

Raw CSV files are retained under the ignored directory
`build/benchmarks/plane-hints-port-kq7tlnmt/`, as `cpu-unlimited.csv` and
`cpu-range100.csv`. Reproduce the runs with:

```bash
taskset -c 2 ./build/benchmark_visibility --frames 240 --repeats 9 \
  --model output/godollo.glb > build/benchmarks/plane-hints-port-kq7tlnmt/cpu-unlimited.csv
taskset -c 2 ./build/benchmark_visibility --frames 240 --repeats 9 \
  --model output/godollo.glb --range 100 > build/benchmarks/plane-hints-port-kq7tlnmt/cpu-range100.csv
```

## Production CPU and GPU render checks

The existing tracked `benchmark_tree_card_split` and `benchmark_zombie_crowd`
programs were run before integration at HEAD `239261fcb467`, then rebuilt and
rerun with plane hints. These are separate baseline/hints builds; the map
harness's card modes do not enable or disable plane hints. Each card mode is
compared with the same mode before integration.

GPU: AMD Radeon 660M, Mesa 24.2.8; 1280 × 720, hardware OpenGL, no presentation
or VSync inside the timers. Tree filtering retains the branch's nearest-texel,
nearest-mip default, and the current sunlight shadow target is 1024 square.
All input assets, camera files and render settings were unchanged.

Each map view/card mode has six rounds of 64 measured frames after capture and
switch warmups. The standard summarizer reports the median of the six round
means. GPU timestamps are asynchronous; query polling/drains and outstanding-work
limits are outside CPU render timing. CPU render includes culling, preparation
inside the renderer, draw submission and driver stalls. GPU and CPU overlap;
do not add these values.

These map cameras stay fixed. A separate deterministic player collision and
undergrowth route runs under the CPU simulation timer; its camera does not
follow that player. There is no AI, audio or dynamic physics. The map camera
file supplies two forest interiors, aerial and clearing views; the harness adds
grounded old-forest views with and without the 100 m limit plus a town view.
This is an isolated rendering comparison, not a full gameplay-route measurement.

### Production default: split tree cards, mode 1

| Map view | Before CPU ms | Hints CPU ms | Before GPU ms | Hints GPU ms |
| --- | ---: | ---: | ---: | ---: |
| `osm:4:2:0` | 9.323 | 7.090 | 9.592 | 7.419 |
| `osm:-6:-5:0` | 5.224 | 4.445 | 7.232 | 6.769 |
| `aerial` | 0.400 | 0.381 | 5.883 | 5.781 |
| `open-clearing` | 5.617 | 5.098 | 5.225 | 4.900 |
| `old-forest-100m` | 0.435 | 0.400 | 6.012 | 5.868 |
| `old-forest-unrestricted` | 8.534 | 8.467 | 8.608 | 8.671 |
| `town` | 4.606 | 4.525 | 4.115 | 4.073 |

### Additional control: original tree cards, mode 0

| Map view | Before CPU ms | Hints CPU ms | Before GPU ms | Hints GPU ms |
| --- | ---: | ---: | ---: | ---: |
| `osm:4:2:0` | 9.627 | 6.763 | 9.772 | 7.323 |
| `osm:-6:-5:0` | 4.949 | 4.534 | 7.153 | 6.862 |
| `aerial` | 0.413 | 0.413 | 5.918 | 5.780 |
| `open-clearing` | 5.285 | 5.267 | 5.032 | 4.987 |
| `old-forest-100m` | 0.442 | 0.360 | 6.172 | 6.034 |
| `old-forest-unrestricted` | 8.976 | 8.510 | 9.175 | 8.707 |
| `town` | 4.598 | 4.696 | 4.096 | 4.129 |

### Required open-area crowd control

The standard `zombies-open-100k-v1` case used all ten actual characters with
three prepared mesh LODs on a synthetic 1,200 × 1,200 m field. Every measured
frame submitted exactly 100,000 zombies, with normal mesh/animation LOD,
distance culling disabled and the same deterministic 60 Hz animation timeline.
CPU simulation ran idle behavior with the observer outside activation range;
audio, player movement and dynamic physics were absent.

| Case | Before CPU ms | Hints CPU ms | Before GPU ms | Hints GPU ms |
| --- | ---: | ---: | ---: | ---: |
| `zombies-open-100k-v1` | 18.692 | 17.955 | 15.049 | 14.730 |

### Interpretation and verification

Whole-render timings are descriptive measurements from sequential runs. The
map's default-card cases showed an observed geometric mean change of −9.4% CPU
render and −6.0% GPU time. The crowd control also shifted by approximately −3.9%
CPU render and −2.1% GPU time. Plane ordering changes CPU work; identical draws,
triangles and captures do not establish a GPU speedup. The sizable GPU shifts
and CPU driver-stall changes can reflect cross-run variation in clocks, thermal
state, scheduling and driver behavior. They cannot be attributed to the
culling heuristic. The isolated CPU query benchmark above measures its effect
more directly and also exposes the range-limited regressions.

All 11,520 expected GPU measurements completed: 384 per view/card mode per build
and 384 per crowd-control build. The map metadata, camera/view records, asset
metrics and all 84 geometry rows were identical before/after. All 15 capture
pairs matched byte for byte: fourteen map/card captures and one crowd capture.
The fifteen affected CTest checks also passed, covering pure culling and the
forest/render integration. There were no missing visibility results, changed
render counts or image differences in these checks.

Raw timings, screenshots, metadata, summaries and verification evidence are in
`build/benchmarks/plane-hints-port-kq7tlnmt/`, with separate `baseline-map`,
`hints-map`, `baseline-zombies` and `hints-zombies` subdirectories. The saved
camera file belongs to the current map; use matching map-relative cameras for
other datasets. Each phase used the following commands, replacing `PHASE` with
`baseline` or `hints` after building the corresponding source revision:

```bash
cmake --build build --target benchmark_tree_card_split benchmark_zombie_crowd -j4
./build/benchmark_tree_card_split output/godollo.glb output/trees.instances.json \
  output/osm/forest_patches.json build/forest_perf/billboard-views.txt \
  build/benchmarks/plane-hints-port-kq7tlnmt/PHASE-map
python3 tools/summarize_render_benchmark.py \
  build/benchmarks/plane-hints-port-kq7tlnmt/PHASE-map
./build/benchmark_zombie_crowd assets/zombies/models \
  build/benchmarks/plane-hints-port-kq7tlnmt/PHASE-zombies
python3 tools/summarize_render_benchmark.py \
  build/benchmarks/plane-hints-port-kq7tlnmt/PHASE-zombies
```
