# Standard rendering benchmarks

Benchmark **source code is tracked in Git** under `tests/benchmark_*.cpp`, with
optional CMake targets. `tools/summarize_render_benchmark.py` is the tracked,
dependency-free CSV report generator. Run commands from the repository root.
These programs are opt-in experiments, not timing assertions in CTest.
The CMake targets require `GD_VIEWER` and `BUILD_TESTING` (enabled by default).

Vegetation now defaults to nearest texels and nearest mip levels, including
shadow alpha masks. Record the filtering mode when comparing new runs with
historical results. For authored filtering, benchmark harnesses can call
`Renderer::setTreeNearestFiltering(false)`; the game uses
`--no-tree-nearest-filtering`.

Raw timings, screenshots and generated reports belong under
`build/benchmarks/<case-or-run-name>/`, which is ignored by Git. Older local
results remain in `build/forest_perf/`, `build/hiz_perf_final/` and
`build/undergrowth-*.csv`; several older one-off forest harnesses exist only in
`build/forest_perf/` and are not tracked. Do not delete those historical artifacts.

For renderer optimizations, retain the applicable map views (forest interiors,
100 m old-forest cutoff, clearing, aerial and town) **and run the open 100,000-zombie
case below**. A claim about typical gameplay should additionally use a repeatable
gameplay route, with the same AI/physics/audio workload before and after.

## Required case: `zombies-open-100k-v1`

This is a rendering stress case using all ten actual zombie characters and their
three prepared mesh LODs. It builds a synthetic, flat **1,200 x 1,200 m** field in
memory, places exactly **100,000 zombies** with the normal deterministic placement
code within a 500 m radius, and frames the whole crowd. It neither reads nor
regenerates the town or changes `config.json`.

The normal mesh/animation LOD policy stays enabled. Zombie distance culling is
disabled, and **every measured frame must submit all 100,000 instances**. A run
fails if that invariant is broken or any required character/LOD is missing. The
overview normally chooses the lowest mesh LOD for the entire crowd; its exact
LOD, triangle and draw counts are recorded. This is not a full-detail geometry
benchmark and does not represent 100,000 active AI pursuers.

Resolution is 1280x720. Animation follows the same deterministic 60 Hz timeline
on every round, including stable per-character phase offsets. Ground fog and haze
are disabled for an inspectable capture. Ordinary sun shadows remain enabled;
camera framing can put the crowd outside shadow range, and actual shadow LOD
counts are recorded rather than assuming those actors cast shadows.

CPU simulation timing runs the real idle-zombie behavior update with an observer
outside activation range. CPU preparation times `Renderer::updateZombies()`;
CPU rendering includes visibility, LOD selection, instance streaming and draw
submission. Audio, player movement and dynamic rigid-body physics are absent.

```bash
cmake -S . -B build
cmake --build build --target benchmark_zombie_crowd -j4
./build/benchmark_zombie_crowd assets/zombies/models build/benchmarks/zombies-open-100k
.venv/bin/python tools/summarize_render_benchmark.py build/benchmarks/zombies-open-100k
```

Use a hardware OpenGL 3.3 context with display access. Assets stay local and are
not redistributed; see [zombie asset setup](../assets/zombies/README.md) if the
original characters or LODs are missing. No online download occurs in this case.

The normal renderer produces six baseline rounds, each with 32 warmup frames and
64 measured frames. On the Hi-Z experimental branch, the same executable also
alternates baseline and `tree-hiz` rounds. **Hi-Z currently affects only vegetation**;
this case has no trees, so its pyramid and vegetation tests are skipped. It is a
crowd regression control, not evidence of zombie occlusion-culling benefit.

The executable prints population, draw and triangle counts per round and writes:

- `*-round-N.csv`: per-frame GPU, CPU render, simulation, preparation, other CPU
  and presentation timings in milliseconds.
- `metadata.txt`: versioned scenario settings, GPU/driver, camera, actual render
  counts and input model/LOD-manifest SHA-256 fingerprints.
- `baseline.ppm` and, where applicable, `tree-hiz.ppm`: captures at the same pose.

The summarizer prints a timing report and writes `report.txt` and `summary.csv`.
It reports GPU, CPU render and CPU non-render medians, 95th percentiles and the
median of round averages. Missing GPU measurements are excluded and counted;
they are never reported as zero. Startup/loading costs are outside frame timers.

GPU timestamps are collected asynchronously. The harness caps work in flight
and polls/drains queries outside measured CPU sections. No window presentation or
VSync is timed, so results measure render cost, **not end-to-end FPS**. CPU render
includes driver stalls. GPU and CPU overlap; do not add them. Run comparisons
sequentially with the same hardware/settings and without other GPU workloads.
GPU clock, temperature and background load can still introduce variation.

Recorded example: [Radeon 660M, 2026-10-20](benchmarks/zombies-open-100k-2026-10-20.txt).
This is a dated example, not a performance threshold for other machines.

## Interpreting GPU time under CPU load

On the tested Radeon 660M integrated GPU, heavy CPU simulation increased GPU
elapsed time for unchanged rendering. A matched 1,600-zombie scenario measured
4.16 ms with movement frozen and 14.58 ms with movement running; final captures
were pixel-identical. Artificial CPU computation reproduced much of the increase,
while an equal-length sleep had a substantially smaller effect. Independent GPU
timestamps confirmed it. See the [controlled results and diagnostic protocol](benchmarks/cpu-gpu-overlap-2026-10-22.txt).

For gameplay comparisons, keep CPU workload and CPU/GPU overlap comparable as
well as camera, geometry and render settings. GPU elapsed time includes stalls
and reflects clock/scheduling conditions; it does not establish increased shader
or animation work. A CPU optimization can change GPU timings indirectly. This
effect was measured on one hardware/driver combination; shared power/clock
behavior is a hypothesis, not an isolated root cause or a universal GPU rule.
Use asynchronous timing in production. Completion waits and frozen simulation
are diagnostic controls that change the workload, not proposed performance fixes.

The [battery-powered torso shader and physics controls](benchmarks/torso-cost-2026-10-23.txt)
isolate wound shader and cavity costs, record a substantial timing change with
the same reported power profile, and include a real-asset ragdoll settling probe.

## Other tracked benchmark programs

| Case | Source / target | Output |
| --- | --- | --- |
| Map vegetation Hi-Z A/B/C, experimental branch | `tests/benchmark_tree_hiz.cpp` / `benchmark_tree_hiz` | Per-frame CSVs and captures; common summarizer supports its round files |
| Frustum rejecting-plane hints | `tests/benchmark_visibility.cpp` / `benchmark_visibility` | [Five scenes, three camera motions, optional horizontal range](VISIBILITY_BENCHMARK.md); CPU query timings and exact ordered visibility checks |
| Undergrowth contact detection | `tests/benchmark_undergrowth.cpp` / `benchmark_undergrowth` | CSV timings on stdout; microseconds per update |
| Healthy tree trunk/canopy geometry A/B | `tests/benchmark_tree_card_split.cpp` / `benchmark_tree_card_split` | Alternating original/split rounds, geometry counts, asset split metrics and captures |
| Sun shadow-map resolution A/B | `tests/benchmark_shadow_resolution.cpp` / `benchmark_shadow_resolution` | 2048² versus 1024²; total-frame and full sunlight-shadow GPU/CPU timings, captures |
| Grenades with/without tree contacts | `tests/benchmark_grenade_trees.cpp` / `benchmark_grenade_trees` | CSV stage timings on stdout; microseconds per update |
| Minimap road overlay | `tests/benchmark_minimap_roads.cpp` / `benchmark_minimap_roads` | HUD-only baseline/roads CSV, CPU/GPU medians and geometry metadata |

### Minimap road overlay

```bash
cmake --build build --target benchmark_minimap_roads -j4
./build/benchmark_minimap_roads output/godollo.glb build/benchmarks/minimap-roads
```

This hardware-GL test draws only the shared HUD at 1280x720, with its 108x108
pixel minimap covering 300 m. It alternates baseline and road-overlay order over
four rounds, each with 512 warmup and 512 measured frames. Both modes follow the
same 100 m radius circle around the loaded map origin on a fixed 60 Hz timeline.
Road preparation and upload happen outside frame timers. `metadata.txt` records
the GPU/driver, preparation time, triangle/cell counts and static buffer bytes;
`frames.csv` contains per-frame CPU/GPU times and `report.txt` their medians.
The road mode includes its 4× MSAA pass, resolve and compositing; target
allocation occurs during warmup. An optional third argument selects a HUD
directory, so a saved control executable can use its matching RML/CSS/shaders
when comparing renderer versions.

CPU time covers `Hud::draw`; asynchronous GPU queries cover the HUD pass.
Query collection, completion waits and presentation are outside CPU timing.
The 3D scene, simulation, audio and VSync are absent, so these results isolate
HUD cost and cannot establish a gameplay FPS change. Very small GPU differences
can be dominated by clock/scheduling variation; inspect round consistency before
attributing a difference to roads.

### Other benchmark inputs

The map Hi-Z case takes `MAP TREES FORESTS CAMERA_FILE OUTPUT_DIRECTORY`.
Camera-file lines are `INDEX NAME eye X Y Z yaw DEGREES pitch DEGREES` in runtime
meters. Keep the matching camera file with a run's results: those coordinates
belong to that map/frame, and must not be reused blindly for another dataset.

The tree-card split case uses the same five arguments as the map Hi-Z case:

```bash
cmake --build build --target benchmark_tree_card_split -j4
./build/benchmark_tree_card_split output/godollo.glb output/trees.instances.json \
  output/osm/forest_patches.json build/forest_perf/billboard-views.txt \
  build/benchmarks/tree-card-split
.venv/bin/python tools/summarize_render_benchmark.py build/benchmarks/tree-card-split
```

Supply a camera file for your map (the example refers to an existing local
capture). The harness adds grounded old-forest views with/without the 100 m limit
and a town view derived from scene geometry. It requests OpenGL 3.3, alternates
six rounds of 64 frames per mode, and bounds outstanding GPU queries outside CPU
timers. Mode 0 uses original cards; mode 1 uses the preuploaded split index range.
CPU simulation measures a fixed collision/undergrowth route; no AI, audio or
dynamic physics runs. Image analysis and all asset preparation are outside frame
timers. Run the standard zombie case as a separate regression control.

Recorded example: [tree-card split on Radeon 660M, 2026-10-20](benchmarks/tree-card-split-2026-10-20.txt).

The sunlight shadow-resolution case uses the same five map/camera arguments:

```bash
cmake --build build --target benchmark_shadow_resolution -j4
./build/benchmark_shadow_resolution output/godollo.glb output/trees.instances.json \
  output/osm/forest_patches.json build/forest_perf/billboard-views.txt \
  build/benchmarks/shadow-resolution
.venv/bin/python tools/summarize_render_benchmark.py build/benchmarks/shadow-resolution
```

Mode 0 is the original 2048-square sunlight map; mode 1 is 1024-square. The
sunlight projection snaps to each target's actual texel grid. Authored texture
filtering, card splitting, shadow distance, coverage, geometry and simulation
are held fixed. It uses the seven daytime map views and adds two flashlight
controls where sunlight is inactive and the flashlight target remains 1024.
Six alternating rounds of 64 frames follow capture/switch warmups. Target
allocation and warmup fences run outside timers. Full-frame GPU timestamps and
the full sunlight-pass elapsed queries are asynchronous; waits/presentation
are outside render timers. `sun-shadows.csv` reports per-round average GPU/CPU
time for map clearing and every sunlight caster category, plus sample counts.
Night controls have zero sunlight-pass samples because that pass does not run;
their total-frame timestamps must still all be present. CPU shadow timing
includes query bookkeeping. Run the standard 100,000-zombie control separately.

Recorded example: [sunlight map resolution on Radeon 660M, 2026-10-20](benchmarks/sun-shadow-resolution-2026-10-20.txt).

## Scenario movement CPU microbenchmark

```bash
cmake --build build --target benchmark_zombie_scenario_motion -j4
./build/benchmark_zombie_scenario_motion
```

Use a Release build. This headless fixture advances 1,600 preplanned actors over
32,768 sloping terrain triangles and 16,384 navmesh polygons, at 60 Hz for 120
steps in each of five trials. It measures route following, guarded physical
movement and final navmesh validation. Setup is excluded. The output includes
reused segment-check counts and a final-position checksum.

This isolates the scenario movement pipeline; it excludes perception,
spatial/render updates, crowd, trees, live props and rendering. It is not a
whole-viewer CPU/GPU benchmark or a prediction of real-map frame times.

## Real-map zombie update substeps

The viewer's existing `--profile` now prints milliseconds and query calls per
zombie update for ground queries, capsule contact queries, navmesh segment
coverage, Detour velocity selection and spatial membership updates. The total
covers only `ZombieLayer::updateBehavior()`, excluding weapons and audio from
the older broad `behavior` timer. Stage times are exclusive; the remainder
includes other navigation/behavior work and timer overhead. Detailed clocks
are disabled without `--profile`.

The contact breakdown now separates candidate refresh, triangle, trunk, door
and prop work; ground separates static geometry and props. Parent stage lines
contain self-time: sum their children to obtain inclusive contact/ground totals.
Door candidate and overlap counters distinguish broad-phase work from actual
collisions.

The remaining update work is also split into behavior dispatch, scenario route
control, navigation control, path following, steering trunk lookup/refresh,
body movement, crowd sweeps, root locomotion pose and render notifications.
These parent lines remain exclusive. Headless CSVs include crowd move, neighbor,
budget-stop and empty-sector bypass counts. The [route/crowd/pose evaluation](benchmarks/zombie-route-efficiency-2026-10-24.txt)
records selected changes, trajectory checks and actual viewer measurements.
The [distant traveler evaluation](benchmarks/zombie-distant-travel-2026-10-24.txt)
compares navigation fast paths, physical contact-region proofs and reduced-rate
travel with swept geometry. The selected approach has a full-movement control.
The [overlap recovery and route-progress follow-up](benchmarks/zombie-overlap-recovery-2026-10-24.txt)
measures 60-second trajectories, separates ground geometry from other contacts,
and compares periodic correction against the retained movement model.
The [route-progress and remaining movement experiments](benchmarks/zombie-route-progress-2026-10-24.txt)
measure corner reconnection, stationary-update skips and rejected ground/facing/
longer-segment alternatives, including per-actor stall comparisons.
The [ground-support/contact sharing experiment](benchmarks/zombie-ground-support-2026-10-24.txt)
records a rejected small-gain prototype and the unchanged ground-query policy.
See the [contact and ground evaluation, 2026-10-24](benchmarks/zombie-contact-efficiency-2026-10-24.txt)
for the measured decisions and viewer limitations.

For viewer movement comparisons, add `--fixed-step --frames N` to advance the
simulation by exactly 1/60 s each frame. Without that diagnostic flag, the viewer
uses wall-clock delta, so faster runs cover a different amount of simulated time.
`--animation-time` freezes the render animation input, not the simulation clock.
Compare longer trajectories and actor-by-actor progress/stalls too: a short-run
speedup or one final overlap snapshot can miss accumulating failures.

`godollo_viewer --frame-profile FILE` saves asynchronous per-frame GPU and CPU
CSV timings independently of `--profile`, allowing viewer comparisons without
fine-query clocks. Rows may arrive out of frame order; filter warmup by `frame`.
GPU timings can be NaN when unavailable. CPU submission/preparation can include
driver waits, so those columns are not pure arithmetic costs.

For repeatable headless measurements of the current scenario:

```bash
cmake --build build --target benchmark_zombie_updates -j4
./build/multiplayer/benchmark_zombie_updates output/godollo.glb \
  scenarios/test-horde/scenario.toml build/benchmarks/zombie-update-substeps
```

Optional trailing `SPAWN_X SPAWN_Z` chooses the stationary player's position;
a following `full` or `optimized` selects the scenario movement model.
The benchmark uses the shared scene loader with vegetation, static geometry,
doors and live-prop collision queries. It alternates three detailed and three
control runs, reloading the seeded population each time. Each run warms up for
60 steps and measures 300 at 60 Hz. It verifies identical final poses and no
runtime path requests. Rendering, audio, networking and physics advancement
are excluded; this is a static-scene zombie update measurement.

Raw CSVs and scene/timer metadata go in the requested output directory. Report
both control and detailed totals to reveal instrumentation overhead. On x86-64
with GCC-compatible intrinsics, an invariant counter is calibrated on one CPU;
query samples that migrate CPUs are omitted and counted. Other platforms or
failed calibration use `steady_clock`. Counter calibration and setup are
excluded from the measurements. For controlled Linux comparisons, `taskset -c
0` pins the benchmark to CPU 0; choose an allowed CPU for the host. The viewer
does not change CPU affinity. Fine-grained timers can be expensive when the
host's clock source requires slow reads; do not treat heavily perturbed stage
times as normal execution costs.

Measured example: [1,600-zombie real-map substeps, 2026-10-23](benchmarks/zombie-update-substeps-2026-10-23.txt).

Cumulative optimization results: [contact and coverage experiments, 2026-10-24](benchmarks/contact-coverage-first-four-2026-10-24.txt).
The real-map harness now also saves `final-poses.bin` for exact cross-version
final-pose comparisons, and CSVs include certified-region reuse/build counters.

For differential collision experiments, build `capture_collision_queries` and
run `./build/capture_collision_queries /tmp/collision.bin`. Compare captures
from two builds on the same platform with `cmp`: the fixture exercises 48,000
capsule contacts and 96,000 cached/uncached ground queries, including slopes,
edges and large world coordinates. The explicit-field binary is a local test
artifact, not a portable runtime format.

The separate `validate_zombie_travel` target performs untimed real-map quality
checks for 600 steps: physical contacts, padded contacts, navmesh containment,
coordinate-precision-aware speed bounds, ground offsets and crowd separation.
It does not run alongside timing measurements. An optional final-pose capture
checks a saved variant against the same scene without advancing it.

```bash
cmake --build build --target validate_zombie_travel -j4
./build/multiplayer/validate_zombie_travel output/godollo.glb \
  scenarios/test-horde/scenario.toml build/benchmarks/travel-quality optimized -526.465 296.095
```

The X/Z arguments are fixture coordinates for the current map, not generic map
centers. Physical contacts use zero skin; padded contacts are reported separately.
The quality case uses current collision queries after each update, with fixed
prop/door poses and no physics advancement. It is not an exhaustive guarantee
for moving props, different maps or every terrain/obstacle configuration.
