# Potential Surface Steering without pathfinding

The fourth experimental workspace is `gdll3/project-gdll-potential-surface`,
branch `experiment-zombie-potential-surface`, based on steering commit `9ea92a2`.
It sits beside the original continuous, grid/JPS and navmesh workspaces.
The standalone C++ prototype has **no path, waypoints, navmesh, route search,
passing-side state or recovery**: each update aims at the destination, evaluates
nearby obstacle potentials, filters the desired velocity, and clips unsafe moves.

**56 of 1,000 agents reached their destinations; 944 stalled.** The outcomes,
recorded display traces and query counts repeat exactly across the warmup and all three
measured passes. The update work is inexpensive, but this version does not
replace global pathfinding on the tested map. Every stalled agent experienced
collision clipping and finished at a padded physical obstacle boundary.

## Actual-map workload

The cached original replay and aerial raster are reused without regenerating
the town. All **171,724 obstacles** participate:

| Kind | Count | Field approximation | Physical collision |
| --- | ---: | --- | --- |
| Building | 9,981 | Circle centered on the rectangle, radius `hypot(halfX, halfZ)` | Original oriented rectangle |
| Tree trunk | 147,495 | Circle using the recorded trunk radius | Exact planar circle |
| Fence panel | 14,248 | Capsule along its longer axis, radius equal to its short half extent | Original oriented rectangle |

Fence capsules provide a long, narrow field with rounded ends. Building circles
are soft cost approximations; they do not become collision geometry or exclude
otherwise clear endpoints. The original replay supplies projected rectangles
and trunk radii, not terrain heights or full rendered building solids. Roads
remain ordinary free space, with no attraction or traversal weighting.

The source replay is the original `project-gdll/build/zombie-pathfinding-relative-offsets/paths.json`:
SHA-256 `886833659e1b1f12488eafb1830240ebb6501c43814dc447ecd8474bf0a90047`.
The raster spans approximately 6 × 6 km, using runtime X east / Z south meters.

Start and destination are independent uniform map samples, rounded to float32
and rejected only against padded physical geometry and the inset map boundary.
Seed **1515146562** produces 2,000 endpoints from 2,172 candidates. There is no
component filtering, nearest-point snapping, or rejection based on the larger
building field circles. Exact requests and input fingerprints are retained.

Straight-line trip distances range from **51.46 to 7,311.14 m**, median
**3,181.26 m**, mean **3,236.93 m**. Global reachability is not classified;
some destinations can lie across disconnected fence/building regions.
NPC-to-NPC collision, terrain following, animation and rendering are absent.

## Fixed steering and stopping rules

Desired velocity points directly at the destination, with a **2 m/s** cap and
a **0.05 s timestep**. The inherited one-sided filter is unchanged:

```text
g = sum of nearby potential gradients
v = desired - weight * max(0, dot(g, desired)) * g / (1 + weight * dot(g, g))
weight = 16
```

No normalization restores speed removed by the filter. Arrival means within
0.15 m of the goal. Each agent receives
`max(120 s, 3 * straight_distance / speed + 60 s)` of simulated time, rounded up
to a timestep. An agent is marked stalled when it has not displaced at least
0.05 m from its current movement anchor over 10 s. Moving 0.05 m resets that
anchor and clock. Stalled agents stop updating; there is no hidden replanning.

Unlike the earlier tree-only test's radius-scaled bump, this experiment uses a
**fixed-meter clearance profile**, so large buildings retain a useful gradient.
For distance `d` from a circle center or nearest capsule-centerline point:

```text
c = d - (primitive_radius + body_radius + skin + building_extra_if_building)
L = 1 m; F = 3 m
U(c) = 1 - c/L                                      if c < 0
       2 * (1 - (c/F)^2)^3 / (1 + (1 + c/L)^2)     if 0 <= c < F
       0                                           otherwise
```

The derivative is analytical, including the smooth cutoff. The surface value
and first derivative agree at `c = 0`; value and gradient vanish at `F`.
The inverse-quadratic profile and compact support avoid needing the entire
surface or summing distant tails. Parameters were fixed before the final run
and were not tuned after observing its outcomes.

Two prebuilt balanced BVHs index field support bounds and physical geometry.
Queries stream every candidate through fixed traversal stacks, with no neighbor
truncation or per-update allocation. Average field leaf candidates are **5.04
per update**, of which **0.407** contribute nonzero potential, rather than
scanning all 171,724 obstacles. These candidate counts include leaf neighbors
outside a primitive's exact support; the exact field test discards them.

Physical padding is a 0.30 m body radius plus 0.002 m skin, with another 0.05 m
for buildings. Rectangles expand along their own axes, matching the earlier
conservative planar obstacle model. Trees use circles. The map boundary is
inset by body radius plus skin. Movement clips at the earliest contact, backs
off by a positive 1 µm, and strictly rechecks the whole accepted segment. Contact
is forbidden; physical shapes are never shrunk by a validation tolerance.
This is **stop-only clipping**, without contact sliding or full game physics.

## Completion and failure analysis

| Outcome | Agents |
| --- | ---: |
| Arrived | **56 (5.6%)** |
| Stalled | **944 (94.4%)** |
| Time budget exhausted | 0 |
| Invalid endpoint | 0 |

The 56 successful trips have a median travel/straight-distance ratio of
**1.00443**; the longest successful traveled distance is **5,952.86 m**.
Their median simulated arrival time is **610.075 s**, maximum **3,050.95 s**.
Ratios describe only successful agents; they are not a whole-workload quality
claim. The median reduction in remaining goal distance across all agents is
approximately **13.0%**.

A post-run diagnostic checks the nearest original padded obstacle at each
stalled endpoint. All 944 have recorded clipped moves and are within 10 µm of
the corresponding physical boundary:

| Nearest contact geometry | Stalled agents |
| --- | ---: |
| Building | 627 |
| Fence | 307 |
| Tree trunk | 10 |

The 10 µm diagnostic threshold labels near-contact endpoints; it is **not** a
collision tolerance. The independent safety audit still forbids contact and
reports zero collisions. This strongly locates the observed problem at building
and fence contacts, but does not isolate its cause to the field approximations.
The finite soft penalty permits uphill motion, neighboring gradients can
combine unfavorably, and the clipping guard does not find another movement.

There is also a structural restriction: for nonzero desired velocity and
finite gradient, the filtered velocity has positive dot product with the
desired direction. With direct goal attraction, it cannot deliberately retreat
away from the goal. Clipping only shortens that movement. Some routes require
such retreat; even an exact head-on convex obstacle can stop this particular
filter. We have not established that all 944 requests were globally reachable,
or that every observed stall required backtracking. These results concern this
specific controller, field representation and collision policy, rather than
every possible potential-field method.

## CPU, elapsed time and memory

AMD Ryzen 5 7535HS, GCC 14.2.0, Release `-O3 -DNDEBUG`, one serial process pinned
to CPU 2, recorded 2026-10-21. There is one warmup and three measured passes.
Only two elapsed-clock and two process-CPU-clock probes run per batch timestep;
there are no per-agent timing probes. Native validation, trace collection,
binary movement output, parsing, spatial-index construction and image rendering
are outside the update timers.

| Measurement | Result |
| --- | ---: |
| Mean CPU time per tick, all 1,000 active | **1.225 ms** |
| CPU time per tick, median / p95 | 1.156 / 1.604 ms |
| Mean elapsed time per tick, all 1,000 active | 2.731 ms |
| Elapsed time per tick, median / p95 | 1.288 / 5.533 ms |
| Whole-pass update CPU time, median | 7.486 s |
| Whole-pass update elapsed time, median | 17.509 s |
| Agent updates per pass | 6,836,087 |
| Shared obstacles, field primitives and both BVHs | 24,150,920 B |
| Final-pass simulation scratch | 124,000 B |
| Whole benchmark process peak RSS | 205,192 KiB |

Fixed-load statistics pool the first 100 steps of all three measured passes:
**300 ticks with exactly 1,000 active agents**. At 20 Hz, that measured CPU
work corresponds to about **2.45% of one core**, excluding the other game work.
Whole-pass totals include the changing active population as agents arrive or
stall; they must not be interpreted as constant-load tick costs.

The C++ process takes **85.04 s elapsed and 42.46 s process CPU** including the
warmup, all measured passes, native validation, parsing and movement output.
That is about 49.9% utilization of one core during this offline run. Scheduling
and shared CPU availability affect elapsed time; no cause is assumed. Final
Python auditing and rendering are additional work outside that process profile.

This workload and implementation differ from the earlier five-way woodland
experiment: batches use fewer timing probes, and movement is standalone 2D
clipping instead of the full `moveZombieBody` pipeline. The earlier 16–18 ms
numbers and this run's CPU costs do not establish a controller speedup. Likewise,
whole-trip simulation time is not comparable to an initial navmesh path request.
No path arena or search workspace is allocated. Sparse display traces, JSON
parsing and full movement logging are separate from the shared-world payload.

## Verification and reproduction

Both C++ suites and all six Python audit tests pass. Tests include analytical
gradients against independent finite differences, BVH queries against brute
force, geometry/contact boundaries, zero-allocation queries, deterministic
simulation, head-on stalls, and 3,000 rotated rectangle/segment cases checked
against GEOS. Corrupted movement streams must fail auditing.

All four simulation passes pass native validation, including stationary steps.
The final full movement stream contains **6,649,150 accepted moved segments**.
An independent Python/GEOS audit finds **zero collisions, boundary violations,
nonfinite coordinates, invalid IDs, discontinuities or final-position/count
mismatches**, with no tolerance or geometry shrink. Decimated display chords
are not used for physical checks. The binary and all frozen source hashes are
unchanged after measurement.

Build from the new workspace using the existing local JSON dependency:

```bash
cmake -S experiments/potential_surface -B build/potential-surface \
    -DCMAKE_BUILD_TYPE=Release -DPSS_JSON_INCLUDE_DIR=/path/to/tinygltf-src
cmake --build build/potential-surface -j4
ctest --test-dir build/potential-surface --output-on-failure
.venv/bin/python -m unittest discover -s tests -p 'test_potential_surface_audit.py' -v
.venv/bin/python tools/run_potential_surface_experiment.py \
    --binary build/potential-surface/benchmark_potential_surface \
    --scene /path/to/original/paths.json \
    --output build/potential-surface-map/new-run --population 1000 \
    --repeats 3 --warmups 1 --seed 1515146562 --cpu 2
```

The standalone build needs only C++17, local `json.hpp`, and OpenSSL Crypto for
input fingerprinting. Python uses the existing NumPy, Shapely 2 and Pillow
environment. Choose a new output directory; previous runs are preserved.

- [Full-map aerial trajectories and close-ups](../build/potential-surface-map/20261021T123318Z/potential-surface-paths.png).
- [Summary and complete agent outcomes](../build/potential-surface-map/20261021T123318Z/simulation/summary.json), [exact requests](../build/potential-surface-map/20261021T123318Z/requests.json), and [tracked three-pass measurements](potential-surface-map-measurements.csv).
- [Independent audit](../build/potential-surface-map/20261021T123318Z/audit.json), [timing analysis](../build/potential-surface-map/20261021T123318Z/timing-analysis.json), and [stall contact diagnostic](../build/potential-surface-map/20261021T123318Z/stall-analysis.json).
- [Source/binary manifest](../build/potential-surface-map/20261021T123318Z/manifest.json), [whole-process profile](../build/potential-surface-map/20261021T123318Z/process.json), and [32-agent pilot](../build/potential-surface/smoke/summary.json).

Images, complete movement streams, frozen binaries and other generated results
remain ignored build artifacts. No image is added to Git.
