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

## Other tracked benchmark programs

| Case | Source / target | Output |
| --- | --- | --- |
| Map vegetation Hi-Z A/B/C, experimental branch | `tests/benchmark_tree_hiz.cpp` / `benchmark_tree_hiz` | Per-frame CSVs and captures; common summarizer supports its round files |
| Undergrowth contact detection | `tests/benchmark_undergrowth.cpp` / `benchmark_undergrowth` | CSV timings on stdout; microseconds per update |
| Healthy tree trunk/canopy geometry A/B | `tests/benchmark_tree_card_split.cpp` / `benchmark_tree_card_split` | Alternating original/split rounds, geometry counts, asset split metrics and captures |
| Sun shadow-map resolution A/B | `tests/benchmark_shadow_resolution.cpp` / `benchmark_shadow_resolution` | 2048² versus 1024²; total-frame and full sunlight-shadow GPU/CPU timings, captures |
| Grenades with/without tree contacts | `tests/benchmark_grenade_trees.cpp` / `benchmark_grenade_trees` | CSV stage timings on stdout; microseconds per update |

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
