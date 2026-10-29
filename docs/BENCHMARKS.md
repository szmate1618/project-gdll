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

Optional `--wounded N` selects the separate `zombies-open-100k-wounded-v1`
control. It gives the first N stable actor IDs a nonempty all-zero vertex mask,
exercising wound rendering and the normal full-detail wounded mesh policy
without removed surfaces or physics. The other actors retain their normal LODs;
all 100,000 must still render. This measures a mixed render workload, not injury
simulation. `--shadow-resolution N` overrides the sun-map size for matched
historical comparisons; omitting it retains the renderer default. Both settings
and actual geometry counts are recorded in metadata. For example:

```bash
./build/benchmark_zombie_crowd assets/zombies/models build/benchmarks/zombies-mixed-100k --wounded 100
```

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
The [pre-bird historical-build comparison](benchmarks/zombies-open-100k-before-birds-2026-10-28.txt)
reruns the exact parent of bird-flock introduction against the current renderer.
The [October 24 build comparison](benchmarks/zombies-open-100k-oct24-2026-10-28.txt)
reruns the last October 24 commit against the October 28 renderer.
The [October 19 renderer comparison](benchmarks/zombies-open-100k-oct19-2026-10-28.txt)
backports only the harness and matches the historical 2048-square sun map.
The [wound shader and severed-resource controls](benchmarks/zombies-open-100k-render-controls-2026-10-28.txt)
test two causes of that gap with four current-renderer variants and identical captures.
The [production healthy/wounded shader split](benchmarks/zombie-wound-shader-split-2026-10-28.txt)
compares the implemented draw partition against the original renderer in healthy
and mixed 100,000-actor cases.
The [October 19/20 source and retained-binary audit](benchmarks/zombie-oct19-oct20-same-session-2026-10-28.txt)
separates revision dates from measurement dates and reruns the saved October 20
executable alongside the October 19 rebuild and current renderer at matched settings.

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
| Map render submission and disabled lights | `tests/benchmark_render_wins.cpp` / `benchmark_render_wins` | Day/night map views, flashlight and driving controls; CPU/GPU CSVs, captures and counts |
| Changed zombie render preparation | `tests/benchmark_zombie_render_preparation.cpp` / `benchmark_zombie_render_preparation` | CPU-only preparation of 100,000 changed walk/run actors; published-pose checks and capture |
| Zombie visibility blocks | `tests/benchmark_zombie_visibility.cpp` / `benchmark_zombie_visibility` | CPU-only maintenance/query CSVs, exact visibility checks and result checksums |
| Zombie LOD image quality | `tests/benchmark_zombie_lod_quality.cpp` / `benchmark_zombie_lod_quality`, `tools/analyze_zombie_lod_quality.py` | Matched poses and views, native color/depth captures, silhouette metrics and interactive report |

### Zombie LOD image quality

This opt-in capture compares the original characters, current LOD1/2/3 and
scratch 35%, 25%, 15% and 10% triangle candidates. Production assets and
`lods.json` are not replaced. All ten characters are sampled from eight yaw
angles and eight phases of idle, walk and run at 12 m and 35 m forward depth.
Those are the normal-FOV entry distances for LOD1 and LOD2. The camera is level
at 1.65 m; all variants share the original asset's 1.8 m normalization.
The optional `--family far` preparation compares LOD3 at 80, 120 and 180 m,
using the active LOD3, 5/3/2% candidates and a finer 10% control. The automatic
meshoptimizer candidates use the historical LOD3 0.16 simplification error limit.

```bash
cmake -S . -B build
cmake --build build --target zombie_mesh_simplify benchmark_zombie_lod_quality -j4
.venv/bin/python tools/prepare_zombie_lod_comparison.py \
  --output build/benchmarks/zombie-lod-quality/assets
./build/benchmark_zombie_lod_quality \
  build/benchmarks/zombie-lod-quality/assets/manifest.json \
  build/benchmarks/zombie-lod-quality/captures shaders
.venv/bin/python tools/analyze_zombie_lod_quality.py \
  build/benchmarks/zombie-lod-quality/captures/samples.csv
```

For the far comparison, pass `--family far` to preparation and use a separate
output directory. Its manifest supplies the three capture distances; the capture
program records the actual current LOD and animation rate from the public policy.

The hardware-GL capture uses the normal baked-animation loader and basic
shaders, authored sRGB materials and default sunlight. Shadows, scene geometry,
fog and postprocessing are absent. Integer-aligned crops (192-square near,
64-square far) preserve the pixel scale of the normal 1280x720, 60-degree vertical
projection. Native color images have no antialiasing; separate depth masks use
four-times linear sampling for
subpixel silhouette comparisons. All meshes use the same displayed stepped
pose, isolating geometry changes from animation-rate changes.

The analyzer requires local NumPy, Pillow and SciPy. It writes an interactive
`analysis/report.html`, per-character contact sheets, per-view metrics and
aggregate CSV/JSON. Boundary distances are symmetric nearest silhouette-edge
distances in native scene pixels; masks are not registered or realigned.
Color errors cover foreground pixels rather than empty background. The report
also records native color coverage and missing reference pixels, which help
identify thin features lost when distant geometry is rasterized. Candidates are
also compared with the current LOD. The eight-pose cycle is a visual
inspection aid, not a full-rate playback or transition test. Neither an image
metric nor an exploratory half-pixel budget establishes human indistinguishability.
This measures appearance and geometry counts, not end-to-end frame-time gains.

Recorded comparison: [LOD1/LOD2 image quality, 2026-10-28](benchmarks/zombie-lod-quality-2026-10-28.txt).
The [LOD3 quality and 100k controls](benchmarks/zombie-lod3-quality-2026-10-28.txt)
cover 80/120/180 m, finer and coarser controls, native coverage loss and a
matched four-process rendering comparison.

The one-character open-column prototype is generated with
`tools/prepare_zombie_prism_lod.py --output build/benchmarks/zombie-prism-lod/assets/ZombieMale_C-open-prisms.glb`
(run with `.venv/bin/python`). It fits ten uncapped triangular columns to the
existing rig: 60 triangles and 60 shared vertices, with source colors baked to
vertex colors. The source rig/animation bytes remain intact. Lower columns
approximate hand/sole extents; no separate hands, feet or end caps are added.
Its comparison variant is `prism`; RGB differences include the approximate
material/color representation, so inspect silhouette and proportions separately.
The torso uses the median of brighter neutral mid-torso texture samples to
retain this character's off-white vest; limb and head colors use nearby samples.

For a LOD4 distance survey, a comparison manifest can set additional camera
conditions (defaults remain level elevation and zero pixel offset):

```json
"capture": {
  "family": "lod4-distance",
  "distances_m": [80, 100, 120, 140, 160, 180, 200, 240, 280, 320],
  "elevations_degrees": [-15, 0, 15],
  "pixel_offsets": [[0, 0], [0.5, 0], [0, 0.5], [0.5, 0.5]]
}
```

Distance is eye-to-aim distance about `(0, 1.65, 0)`. Positive elevation raises
the camera; positive pixel offsets shift the projected figure right/down in
native scene pixels, including when drawing the supersampled mask. Animation
rate follows the nominal distance, so the 80 m reference is forced to LOD3/5 Hz
at every elevation. Production selection instead measures actor-bound-center
distance. These modest elevations do not validate top-down views.

With `original`, `lod3` and `prism` records, analyze this extended CSV using
`tools/analyze_zombie_lod4_distance.py captures/samples.csv` (with `.venv/bin/python`).
It pairs all camera conditions, measures the LOD3-to-prism switch as well as
original-reference quality, and writes per-view metrics, summaries, a native-size
comparison and a same-pose A/B blink. The fixed geometric budgets and native
visibility guard are exploratory; color/shading changes are reported separately.
The sampled poses and blink do not establish continuous-motion acceptability.
Distances beyond the default 180 m cutoff require extended/unlimited draw distance.
The [white-vest LOD4 distance survey](benchmarks/zombie-lod4-distance-2026-10-29.txt)
records 23,040 matched views: the proxy fits the original reasonably at 160-180 m,
but no tested distance through 320 m passes the current-LOD3 switch budgets.
The revised vest color, coverage mismatch and native raster dropout are recorded
separately; runtime LOD policy remains unchanged.

A direct LOD3 replacement is a different comparison: use the existing LOD2 as
the transition reference at 80 m. A capture record may specify `"animation_hz": 10`
to force the LOD2 predecessor's stepped rate, while records without an override
share the nominal-distance rate (5 Hz at 80 m). Zero selects the default policy.
The focused `tools/analyze_zombie_prism_lod3.py` compares the earlier sampled
colors and off-white vest with current LOD3, including matched-pose LOD2 and
separate 10-to-5 Hz sampled transition comparisons. It also reports RGB errors
on a common foreground support so color rankings use the same pixels.
The [80 m comparison of both prism colorings](benchmarks/zombie-prism-lod3-80m-2026-10-29.txt)
records 2,304 matched views. The prism is a promising direct LOD3 replacement
for the tested character, with improved original silhouette overlap and fewer
triangles, while LOD2 transition metrics show a mixed quality tradeoff. Earlier
colors fit the full original better on aggregate; off-white fits LOD2 better
than the earlier proxy coloring. Neither result establishes a global default.

The `slim` prism profile narrows both torso rings by 15% in width/depth without
changing their axial height. It scales only the four terminal hand/foot rings
by 0.7 about their wrist/ankle joints, including terminal overhang; elbow/knee
rings, upper limbs and head stay fixed. With two rings per lower segment, this
changes the taper toward the smaller tip. Both profiles retain 60 vertices,
60 triangles and open ends. Vertex colors are frozen between corresponding
standard/slim variants; normals are recomputed for the new geometry.

```bash
.venv/bin/python tools/prepare_zombie_prism_lod.py --profile slim --color sampled \
  --output build/benchmarks/zombie-prism-slim/assets/ZombieMale_C-prism-slim-original-colors.glb
.venv/bin/python tools/prepare_zombie_prism_lod.py --profile slim --color white-vest \
  --output build/benchmarks/zombie-prism-slim/assets/ZombieMale_C-prism-slim-white-vest.glb
```

The 80 m analyzer accepts a complete additional pair of `prism_slim_original`
and `prism_slim_white` records. It adds paired standard-to-slim improvement
counts and per-angle summaries. Its common RGB support then includes all eight
variants, so compare color rankings within that report rather than mixing its
absolute RGB values with the earlier six-variant report.
The [Prism Slim comparison](benchmarks/zombie-prism-slim-2026-10-29.txt) records
both new color variants, 2,304 matched 80 m views and a separate 12 m inspection.
Median silhouette overlap improves from standard 0.727 to slim 0.756; projected
area improves from 115.8% to 101.5% of original. The report retains thin-pixel
losses, transition tradeoffs and per-angle results alongside those improvements.

`--profile slim2` starts from `slim` and changes only the two upper rear torso
corners. Their separation shrinks another 15% about their midpoint, and both
move down by 10% of the original fitted torso height along its axis. The source
faces +Z: the unchanged single +Z corner is at the front, and the edited pair
is at the rear. The upper rim becomes tilted; no cap or vertex is added. The
same `sampled` and `white-vest` color options apply, with frozen vertex colors.
The analyzer accepts `prism_slim2_original` / `prism_slim2_white` records together
with the Slim 1 baseline pair, and adds paired Slim 2-versus-Slim 1 comparisons
by angle, clip and elevation. All ten variants share one common RGB support.
The [Prism Slim 2 comparison](benchmarks/zombie-prism-slim2-2026-10-29.txt) records
2,304 matched views and a separate 12 m inspection. Median original-relative
overlap falls from Slim 1's 0.756 to 0.750, with losses strongest from the front
and rear. Lowering the rear edge also exposes a neck gap in some poses. Slim 1
remains the better overall fit in this experiment; both Slim 2 color variants
are retained for review.

Slim 1 is now the active LOD3 for all ten types; the normal asset-preparation
command retains valid LOD1/2 and regenerates Slim 1 LOD3. The [rollout and matched
100k benchmark](benchmarks/zombie-prism-slim1-rollout-2026-10-29.txt) records the
ignored backup copy, per-type color policy, conservative LOD bounds and four
old/new/new/old processes. CPU render median falls 15.82 to 12.90 ms; GPU median
falls 12.31 to 8.15 ms, with all 100,000 actors and ten draws retained.

### Zombie visibility blocks

```bash
cmake --build build --target benchmark_zombie_visibility -j4
mkdir -p build/benchmarks/zombie-visibility
./build/benchmark_zombie_visibility build/benchmarks/zombie-visibility/timings.csv
./build/benchmark_zombie_visibility build/benchmarks/zombie-visibility/phases.csv --profile
```

This CPU-only synthetic benchmark needs no map, assets or display. Cases cover
53,000 spread actors with either zero or 1,000 movers, a fully visible dense
100,000-actor crowd, and 1,000 actors separated by large distances. Each case
restarts four rounds with 30 warmup and 120 measured frames on the same fixed
60 Hz timeline. Movers cross fine-cell boundaries during measurement.

CSV rows report per-round mean milliseconds for index updates, queries and their
combined cost. Queries include dirty-bound refitting; setup, pose arithmetic,
result consumption and correctness checks are outside timers. First and final
measured frames must match a full-population oracle for exact IDs, pass masks
and ordering. Every measured frame contributes to a result checksum for
baseline/candidate comparisons. Optional `--profile` adds coarse refit,
traversal/test and sorting timings; use default runs for performance comparisons.
This isolates visibility CPU cost; the required real-asset open 100k case above
remains the rendering regression control.

Recorded comparison: [visibility blocks, 2026-10-28](benchmarks/zombie-visibility-blocks-2026-10-28.txt).
The [sort-removal experiment](benchmarks/zombie-visibility-no-sort-2026-10-29.txt)
compares query savings against downstream instance preparation and full rendering;
the source-ID sort remains enabled after the experiment.
The [radix comparison](benchmarks/zombie-visibility-radix-2026-10-29.txt)
preserves the same ordered IDs and pass masks, using radix passes for larger
result lists and comparison sorting for small ones.

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

## Isolated renderer comparisons

```bash
cmake --build build --target benchmark_render_wins benchmark_zombie_render_preparation -j4
./build/benchmark_render_wins output/godollo.glb output/trees.instances.json \
  output/osm/forest_patches.json build/forest_perf/billboard-views.txt \
  shaders build/benchmarks/render-wins-map 1 128 all
./build/benchmark_zombie_render_preparation shaders build/benchmarks/render-wins-preparation
```

The map case accepts `MAP TREES FORESTS CAMERAS SHADERS OUTPUT [ROUNDS=1]
[FRAMES=96] [CASE_FILTER=all] [--profile]`. Camera files use the format described above;
they must match the published map. The case adds grounded old-forest and town
views, plus forest/town nighttime controls with the flashlight off, on, and
suppressed while driving with flashlight shadows enabled. The optional filter
matches a substring such as `night` or `town`. Vegetation retains its normal
nearest filtering, card splitting and distance policy. No AI, audio, player
updates or dynamic physics runs. Animation input follows a fixed 60 Hz timeline.
Warmup completion, bounded asynchronous query draining and captures are outside
the CPU render timer. Per-view CSVs record GPU and CPU render milliseconds;
metadata records input/shader fingerprints, cameras and draw counts.

For a separate opaque-ordering diagnostic, supply all three optional positional
values and append `--profile`. This also writes `opaque-sort-stages.csv` with
per-round mean CPU milliseconds for opaque source-ID sorting and the measured
call count. Warmup, GPU waits, transparent sorting and drawing are outside this
stage. Keep these diagnostic runs separate from default rendering comparisons.
The [opaque town ordering comparison](benchmarks/town-source-order-radix-2026-10-29.txt)
records the radix experiment, all standard map-view controls and the 100k case.
The [fixed-night alternating control](benchmarks/town-night-sort-control-2026-10-29.txt)
uses both sorters in one renderer and adds GPU timestamps after gathering to
investigate the earlier nighttime intervals separately from CPU sort cost.
The [completed-frame throughput control](benchmarks/town-night-throughput-2026-10-29.txt)
removes per-frame timing and sampling, and includes GPU completion at each
block's end in its wall-clock result.

The preparation case uses a synthetic animated four-vertex marker, with 100,000
instances split between walking and running. Every fixed step changes only
locomotion time. It times `Renderer::updateZombies()` alone, excluding caller
updates, spatial bounds changes, drawing and file output. Eight rounds alternate
visible and hidden camera controls; each has 16 warmup and 128 measured updates.
Both controls update the same actors. Final published-pose checksums and a
visible capture verify the submitted result. Diagnostic GPU completion waits
occur outside the measured CPU loops. This isolates preparation arithmetic;
the standard real-asset crowd case remains the rendering regression control.

For A/B comparisons, retain each executable with its matching shaders and build
all header-dependent consumers together. Alternate baseline/candidate process
order across repeated pairs; avoid concurrent builds or GPU workloads. Treat
external process runs as the independent samples. These cases measure render
cost and preparation stages, not gameplay FPS.

Recorded comparison: [three isolated render changes on Radeon 660M,
2026-10-26](benchmarks/render-wins-2026-10-26.txt).

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

The [53,000-actor corridor visibility investigation](benchmarks/corridor-zombie-visibility-2026-10-27.txt)
separates pursuit from traveler simulation and records an 80-second matched
camera-distance rejection comparison, with unchanged final captures.

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
