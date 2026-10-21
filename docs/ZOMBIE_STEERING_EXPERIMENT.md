# Five-way local tree steering experiment

Branch `experiment-zombie-local-steering` is a child of navigation commit
`dda2085` (`implement-zombie-navmesh-navigation`). This adds an opt-in headless
experiment, not live crowd integration. Global paths still use our tree-free
navmesh and `g + 8h`; the comparison measures following those paths around trees.

**Detour has the highest completion rate: 998/1,000**, followed by the analytical
gradient filter at 994/1,000. The gradient has the lowest observed mean update
time; timing distributions overlap. The perpendicular-push rule reaches only
760/1,000 and produces substantially more turning and travel. I would carry
Detour forward as the more robust integration candidate, retain gradient-only
as the cheaper baseline, and add a separate stuck/replanning policy before live
use. None of these local methods solves every controlled obstacle case.

## Results: 1,000 independent NPCs

Every method receives the same 500 trunks and 1,000 start/goal pairs in a
100 × 100 m synthetic woodland. Each trip is 15–30 m; NPC speed is capped at
2 m/s, updates use 0.05 s steps, and the simulation allows 60 s for arrival.
Arrival means within 0.15 m of the destination. There is no NPC-to-NPC collision,
controller-specific replanning, or automatic recovery after a stall.

| Method | Arrived | Not arrived / stalled | Mean step, all 1,000 active | Step p95 |
| --- | ---: | ---: | ---: | ---: |
| Collision response | 404 | 596 / 596 | 17.032 ms | 21.042 ms |
| Gradient only | 994 | 6 / 6 | 16.256 ms | 19.884 ms |
| Gradient + passing side | 945 | 55 / 55 | 16.749 ms | 21.300 ms |
| Perpendicular push | 760 | 240 / 240 | 17.138 ms | 21.818 ms |
| Detour avoidance | 998 | 2 / 2 | 17.542 ms | 21.798 ms |

The timing column uses the first 100 simulation steps, while **all 1,000 NPCs
are active for every method**. It includes the follower, indexed tree query,
controller, existing movement/collision, common safety guards, instrumentation
and bookkeeping. It excludes path planning, setup, validation and output.
These are instrumented update-loop elapsed times, not rendering frame times or
pathfinding request times. Per-stage clock probes are included and appreciable:
even the no-op collision-only controller has measurable probe overhead.

All **1,000 requests are completed by at least one method**, so the remaining
failures of individual methods are avoidable in this fixture. For the **346
requests completed by all five**, quality comparisons use that identical cohort:

| Method | Median travel / original path | Median arrival | Median heading change per meter |
| --- | ---: | ---: | ---: |
| Collision response | 0.9962 | 10.300 s | 0.005°/m |
| Gradient only | 0.9987 | 10.675 s | 3.145°/m |
| Gradient + passing side | 1.0013 | 10.425 s | 3.812°/m |
| Perpendicular push | 1.1013 | 11.475 s | 24.579°/m |
| Detour avoidance | 0.9961 | 10.300 s | 0.012°/m |

Ratios slightly below one reflect stopping within the 0.15 m arrival tolerance,
rather than reaching the exact endpoint. Heading change per meter measures all
turning, not oscillation alone. This common cohort favors the easier routes that
the collision-only baseline can finish.

Full-run totals are retained in the CSV/JSON but are unsuitable as a fixed-load
speed ranking: early arrivals stop updating, whereas stuck agents keep updating
until the timeout. All unfinished woodland agents meet the recorded stall rule:
no improvement of at least 0.05 m in best direct goal distance in the final 5 s.
This definition can also flag a legitimate wide detour that temporarily moves
away from the goal; it does not prove physical unreachability.

## Controlled obstacles

These cases use the same follower and collision pipeline as the woodland.
Each has one NPC; ✓ means arrival within 60 s, × means nonarrival.

| Case | Collision | Gradient | Gradient + side | Push | Detour |
| --- | :---: | :---: | :---: | :---: | :---: |
| empty | ✓ | ✓ | ✓ | ✓ | ✓ |
| head on | × | × | ✓ | ✓ | ✓ |
| offset tree | ✓ | ✓ | ✓ | ✓ | ✓ |
| passable gap | ✓ | ✓ | × | ✓ | ✓ |
| impassable gap aroundable | × | × | ✓ | × | ✓ |
| sealed gap | × | × | × | × | × |
| overlapping bumps | × | × | ✓ | ✓ | ✓ |
| u trap | × | × | × | × | × |
| dense cluster | × | ✓ | ✓ | × | × |

The 0.9 m trunk gap can admit the 0.6 m body; the 0.4 m gap requires going around
the pair. In `sealed_gap`, a tree blocks the entire 1.4 m navmesh corridor, so
nonarrival is the correct outcome. The U-shaped barrier admits a wide route
around its outside, but every local rule gets trapped. Gradient-only preserves
exact head-on symmetry and cannot invent a side step. Passing-side steering
breaks that symmetry, yet makes the otherwise passable gap fail with these
fixed parameters. Detour's local sampler also lacks a guarantee of escaping
clusters or traps.

## Methods and fixed parameters

All variants share the same waypoint follower, physical radius 0.30 m, height
1.8 m, skin 0.002 m and 2 m/s speed cap. Tree neighborhoods come from the
existing `CollisionWorld` trunk BVH, copied into a preallocated 256-entry buffer.
The default query reaches 3.302 m from the NPC center to the trunk surface;
this covers the complete field support. Larger configured steps enlarge the
query to cover the possible movement sweep. Overflow is reported and stops the
proposed movement; no overflow occurs in this experiment.

1. **Collision response** (`collision_only`): follow the path's desired velocity
   and let the common physical pipeline handle contact. The common tree query
   still runs for fairness and safety; a specialized baseline could omit the
   steering-specific query.
2. **Analytical gradient** (`gradient`): sum compact radial tree bumps and remove
   part of the uphill velocity. For desired velocity `v`, gradient `g`, and
   `λ = 16`, use `v − λ max(0, g·v) g / (1 + λ |g|²)`. Suppressed speed is not
   normalized back to full speed. Each tree contributes
   `(1 − r²/R²)³ / (1 + r²/s²)` inside `R`, zero outside, with
   `s = trunk radius + body radius + skin` and `R = s + 3 m`. Its exact analytic
   derivative includes the compact taper. This is a one-sided local filter,
   not a geodesic solver or collision barrier.
3. **Gradient plus passing side** (`gradient_side`): add a tangential component
   of strength `0.8 × desired speed × urgency`. A forward threat is selected
   within a 1.5 s horizon, and its side is retained until it is behind the NPC.
   A perfectly centered tie chooses left. Urgency fades across the 3 m influence
   range; the common speed cap still applies.
4. **Perpendicular push** (`perpendicular_push`): for each nearby trunk,
   compute `max(0, distance_before − distance_after_proposed_step)` and add
   exactly that much lateral displacement perpendicular to the original desired
   motion, away from the tree. Sum the corrections, divide by the timestep,
   and cap the resulting speed. Persistent side selection resolves centered
   ties. There is no extra avoidance gain.
5. **Detour avoidance** (`detour`): the actual pinned
   `dtObstacleAvoidanceQuery::sampleVelocityAdaptive` samples velocities around
   stationary trunk circles, using the NPC's previous accepted velocity.
   Parameters: velocity bias 0.4; desired/current/side/TOI weights
   2.0/0.75/0.75/2.5; horizon 1.5 s; adaptive divisions/rings/depth 7/2/3.
   This integrates Detour's avoidance component, not DetourCrowd, a replacement
   navmesh, or inter-agent avoidance.

No parameters were tuned after seeing the final workload outcomes. The vendored
subset is unmodified Recast Navigation v1.6.0, commit
`6dc1667f580357e8a2154c28b7867bea7e8ad3a7`, with its upstream license/notices.
All ten upstream source/header/license files were independently byte-checked
against that pinned revision; see the retained verification JSON and
[vendor notes](../third_party/recastnavigation/README.md).

## Common collision safety and allocation limits

Every variant calls the existing `moveZombieBody`, then an experimental common
exact swept-trunk guard, then a navmesh segment guard. Early strict validation
found movements with clear endpoints whose connecting chord cut through a
trunk by less than a millimeter. The added guard clips at the first disk entry
with 0.0001 m backoff and rechecks the accepted float32 segment. It does not
shrink trunks or relax the penetration test. The original failed prototype and
subsequent passing smoke trial are retained in separate directories.

This conservative guard can stop a proposed contact slide. Its interventions
therefore affect completion rates, particularly collision-only and passing-side
steering. **The baseline here is existing movement plus this same guard**, not
an exact measurement of unmodified production sliding. Production movement is
unchanged. A future continuous sliding implementation should be compared again
before drawing conclusions about live crowd behavior.

The controller owns a fixed **16,556 B** workspace per simulation thread, reused
sequentially by agents. All modes construct that same workspace. Tree query and
controller arrays are bounded, and tests check no allocations in the new query,
follower or steering calls. Existing contact handling can allocate; the complete
movement pipeline is not allocation-free. Full-resolution validation history is
separate benchmark storage, up to 9,608,000 B for 1,000 NPCs over 1,200 steps.

`PathManager` retains its original 10 MB arena contract. The harness copies its
precomputed paths into scenario-owned vectors outside steering timers. Every
synthetic scenario has a rectangular, one-polygon tree-free mesh, so its global
paths are straight. Unit tests separately exercise follower corners and holes.
This experiment does not evaluate the full town, buildings/fences, uneven
terrain, crowded NPC interactions, animations, or rendering.

## Measurement and validation

Recorded on 2026-10-21, AMD Ryzen 5 7535HS, Linux, Release `-O3 -DNDEBUG`, pinned
to CPU 2. A single frozen executable runs all methods serially in forward then
reverse order. Each process runs one warmup and three measured passes per case:
**six measured passes per method/case**, 300 measured simulations plus 100
warmups overall. The fixed-load table pools 600 all-active steps per method.
CPU scheduling, clock-probe cost and background load still affect elapsed times.

The source manifest, source snapshot and executable SHA-256 are retained. Scene
geometry and request hashes must match across methods. Repeated processes must
produce byte-identical complete float32 trajectories and identical agent
outcomes. Native validation checks every measured and warmup run using a separate
uniform-bucket index and double-precision circle distances. A second Python/GEOS
audit reads every stored frame in the 50 representative trajectories using an
independent broad phase and closest-point calculation. Display traces are
decimated; their longer display chords are not used for physical validation.

**All 11 focused tests pass.** Every measured pass and warmup has zero endpoint
penetrations, swept penetrations, navmesh violations, query overflows and invalid
sweep starts. Measured native validation checks 6,516,042 moving segments; the
representative Python audit checks 6,028,138 frame segments, including
stationary intervals, with **zero penetrations and zero penetration tolerance**.
Repeated full trajectory hashes and outcome records match exactly. The ten
benchmark processes take 313.77 s of total wall time, including
warmups, setup, validation and serialization; this is separate from simulated
travel time and the update-loop timings. Compiler: GCC 14.2.0.

The JSON names `side_changes` and `obstacle_tests` need precise interpretation:
the former counts passing-side selections, including first selection; the
latter counts supplied candidate trunks, not every internal field/sample test.
`velocity_samples` counts actual Detour samples.

## Reproduce and inspect

From a configured headless navigation build with the existing local dependencies:

```bash
cmake -S . -B build/navmesh-navigation -DGD_STEERING_EXPERIMENT=ON
cmake --build build/navmesh-navigation --target benchmark_zombie_steering \
    test_zombie_steering test_zombie_path_follow test_zombie_steering_simulation \
    test_collision test_tree_collision -j4
ctest --test-dir build/navmesh-navigation \
    -R '^viewer_(zombie_(navmesh|steering|path_follow)|collision$|tree_collision$)' \
    --output-on-failure
.venv/bin/python tools/compare_zombie_steering.py \
    --binary build/navmesh-navigation/benchmark_zombie_steering \
    --output build/navigation/steering/new-run --population 1000 --repeats 3 --duration 60
.venv/bin/python tools/render_zombie_steering.py build/navigation/steering/new-run
```

Use a new output directory: existing runs are preserved. The Python comparison
uses the existing NumPy, Shapely 2 and Pillow environment. There is no GPU or
town regeneration, and Detour builds from local vendored sources.

The retained run is [`build/navigation/steering/20261021T113642.716400Z`](../build/navigation/steering/20261021T113642.716400Z/comparison.json).

- [Controlled paths](../build/navigation/steering/20261021T113642.716400Z/controlled_paths.png) and [all 1,000 woodland paths](../build/navigation/steering/20261021T113642.716400Z/woodland_paths.png): aerial raster images.
- [Comparison JSON](../build/navigation/steering/20261021T113642.716400Z/comparison.json), [source/binary manifest](../build/navigation/steering/20261021T113642.716400Z/manifest.json), and [process profiles](../build/navigation/steering/20261021T113642.716400Z/processes.json).
- [Tracked measurement CSV](zombie-steering-measurements.csv): all 300 measured simulations; complete stage metrics remain in the JSON.
- [Upstream byte verification](../build/navigation/steering/20261021T113642.716400Z/upstream-verification.json).
- [Original strict-validation smoke report](../build/navigation/steering/20261021T113642.716400Z/smoke/summary.json) and [passing guarded smoke report](../build/navigation/steering/20261021T113642.716400Z/swept-guard-smoke/summary.json).

Each numbered process directory contains scene geometry, exact requests, agent
outcomes, per-step CSVs and complete binary trajectories. Sources and executable
are retained under `source-snapshot/` and `binaries/`. Generated files remain
ignored build artifacts. The trajectory format is `GDST001`: 8-byte magic,
little-endian uint32 population/frame count, then frame-major float32 X/Z pairs,
including the initial frame.
