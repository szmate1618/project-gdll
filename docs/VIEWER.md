# Linux GLB viewer

Windows multiplayer client builds are described in
[the multiplayer documentation](../multiplayer/README.md#windows-client).

`godollo_viewer` is a small C++17 desktop viewer for glTF 2.0 scenes. It opens a
resizable 1280 × 720 GLFW window, requests an OpenGL 3.3 Core context, and draws
the scene with a perspective free-fly or first-person walking camera and directional lighting. Startup
prints the OpenGL version, renderer, vendor, and GLSL version. The Python scene
generator remains a separate program.

Use `--fullscreen` to fill the primary monitor and `--resolution` to select
`960x540`, `1280x720` (default), or `1920x1080`:

```bash
./build/godollo_viewer output/godollo.glb --fullscreen --resolution 960x540
```

Fullscreen retains the desktop display mode and renders the scene, held weapon
and reticle at the selected resolution before scaling to the monitor. Black bars
preserve 16:9 on other screen shapes. Screenshots use the selected render size.
Without `--fullscreen`, `--resolution` sets the initial window size; normal
window resizing still adjusts the render target. `--hidden --fullscreen` renders
at the selected size in a hidden window without covering the monitor.

The viewer also plays positional sound through OpenAL Soft, with a camera
listener, preloaded effects, streamed ambience, and a bounded nearby-zombie voice
pool. See [audio setup and replacement sounds](AUDIO.md). Use `--no-audio` to
disable playback while retaining simulation hearing.

## Linux dependencies

Install a C++17 compiler, CMake 3.20 or newer, Python 3, OpenGL development
headers/libraries, OpenSSL and PROJ development headers, and X11 development packages. A GPU driver supporting OpenGL
3.3 and a working desktop display are required for normal interactive use.
Python is used only to generate the tiny test asset; no Python packages or
generator dependencies are needed to build the viewer.

Ubuntu / Debian:

```bash
sudo apt install build-essential cmake python3 pkg-config libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev libssl-dev libproj-dev
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake python3 pkgconf-pkg-config mesa-libGL-devel \
  libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel openssl-devel proj-devel
```

CMake downloads pinned versions of [GLFW 3.4](https://github.com/glfw/glfw/releases/tag/3.4),
[GLM 1.0.1](https://github.com/g-truc/glm/releases/tag/1.0.1), and
[tinygltf 2.9.7](https://github.com/syoyo/tinygltf/releases/tag/v2.9.7), verifying
each archive's SHA-256. It also fetches a pinned Box3D commit for door and ragdoll physics.
Audio uses [OpenAL Soft 1.24.3](https://github.com/kcat/openal-soft/releases/tag/1.24.3),
also pinned and hash-checked, with its built-in HRTF data.
The first configure needs network access. Downloads live
under `build/_archives/`; extracted dependencies and their build products live
under `build/_deps/`. Their examples, tests, and install targets are disabled.
Subsequent builds reuse these files. No global C++ dependency installation is
performed.

OpenSSL's crypto library verifies the map and tree asset fingerprints in a
placement layer. The viewer reads local files; this does not enable downloads.

The default backend is X11, including XWayland in Wayland desktop sessions.
For native Wayland, additionally install `libwayland-dev libxkbcommon-dev
wayland-protocols extra-cmake-modules` on Ubuntu/Debian or `wayland-devel
libxkbcommon-devel wayland-protocols-devel extra-cmake-modules` on Fedora, then
configure with `-DGLFW_BUILD_WAYLAND=ON`. Set `-DGLFW_BUILD_X11=OFF` if you only
want the native Wayland backend.

## Configure, build, and run

From the repository root:

```bash
cmake -S . -B build
cmake --build build -j
./build/godollo_viewer
```

The default build is Debug and requests an OpenGL debug context. Use
`-DCMAKE_BUILD_TYPE=Release` for an optimized build. All compiler output,
executables, CMake files, and dependency code stay in `build/`.

The default build generates `assets/test.glb` from
`tools/create_test_asset.py`, using only Python's standard library. This tiny
textured model can be regenerated at any time:

```bash
python3 tools/create_test_asset.py --output assets/test.glb
```

Open a scene generated in this repository or any other glTF 2.0 scene:

```bash
./build/godollo_viewer output/godollo.glb
./build/godollo_viewer ../generator/output/godollo.glb
./build/godollo_viewer /path/to/scene.gltf
./build/godollo_viewer --help
```

Paths supplied on the command line are relative to the current working
directory. The default asset and shader directory are recorded as absolute
source paths at configure time, so the executable can be launched from another
directory. Reconfigure if you move the checkout. The executable is intended to
run from the checkout; it is not a standalone install bundle.

## Trees

After [preparing tree placements](TREES.md), open the map normally:

```bash
./build/godollo_viewer output/godollo.glb --fps
```

The viewer automatically finds `trees.instances.json` beside the map. It checks
the manifest version and the map/asset SHA-256 fingerprints before loading each
shared tree or pine GLB once. Instance matrices preserve position, independent
horizontal scales, image orientation, and shear. If the adjacent layer is
incompatible, startup explains why it was skipped; an explicitly selected
invalid layer is an error. Regenerate placements after changing the map or
the shared GLBs.

Default preparation combines OSM trees/rows with tuned2 outside OSM forests:
`tools/prepare_trees.py` or `python -m src.generate --trees-only --offline`.
Preparation updates this separate JSON without rebuilding the town. The first
previous dataset is preserved as `trees.original.instances.json` beside the map
and can be selected with `--trees`.

```bash
./build/godollo_viewer output/godollo.glb --trees output/trees.instances.json --fps
./build/godollo_viewer output/godollo.glb --vegetation orthophoto --fps
./build/godollo_viewer output/godollo.glb --vegetation osm --fps
./build/godollo_viewer output/godollo.glb --vegetation sentinel-astra --fps
./build/godollo_viewer output/godollo.glb --no-trees --fps
./build/godollo_viewer output/godollo.glb --no-tree-culling --fps
./build/godollo_viewer output/godollo.glb --no-tree-collisions --fps
```

`--vegetation osm-tuned2|orthophoto|osm|sentinel-astra|none` selects a complete
vegetation preset. The default combines OSM forests, OSM trees and rows, and
additional tuned2 trees, suppressing tuned2 centres within 6 m of OSM trees.
Orthophoto mode uses the preserved CSV detections and original forest polygons.
OSM mode uses only OSM data; Sentinel-Astra generates trees from its polygons.
None disables both forest and individual vegetation. Repeated selections and
combinations with `--trees` overrides are errors. `--forests` was removed. See
[vegetation selection](FORESTS.md#select-one-vegetation-source) and
[OSM preparation](FORESTS.md#optional-openstreetmap-vegetation).

Trees use GPU instancing: visible instances share geometry and textures. Nearby
trees use four draw calls per species for the current four-card assets. A static
spatial index and per-instance bounds cull trees outside the camera frustum. Bounds include
the complete transformed cards, so a tree can remain visible when its trunk is
outside the view. Materials remain alpha-masked and unlit; opaque
parts write depth while transparent texels do not block the background.
Distance haze and ground fog apply to their visible texels, so cards blend with
the rest of the scene. Trees also cast alpha-masked sun shadows into the local
shadow map, using a separate query of the light volume. They retain their unlit
color shading and do not receive sun shadow maps. Approximate neighboring canopy
cover darkens lower foliage in dense stands while leaving exposed tops brighter;
this is calculated once at load and preserves instancing. See
[forest canopy shading](FORESTS.md) for the approximation and costs.
`--no-tree-canopy-shading` disables this daytime effect for comparison.

Visible trees are ordered near-to-far in stable **8 m camera-depth bands** before
being split into species and LoD batches. Depth is measured along the camera's
view direction using each instance's conservative bounds center, including for
aerial views. Trees at 1,016 m or farther share the last band. This can reduce
alpha-mask overdraw while keeping the same geometry and draw-call count.
Ordering only visits the visibility-query candidates and reuses scratch buffers;
it adds linear CPU work and approximately 9 bytes of scratch storage per selected
instance. Order within each band stays stable. Shadows retain their light-query
order, and collision/placement data remain unchanged. Use
`--no-tree-depth-ordering` to compare CPU and GPU time with ordering disabled.

Healthy broadleaf and pine upright cards are analyzed once during upload. A
strong-green foliage test and row-width test locate the start of the narrow
lower trunk region; four source texels of padding protect the split. The upper
quad keeps its full width, while the lower quad encloses the trunk/root texels,
with four texels of horizontal padding. Isolated alpha below 16/255 is ignored
(or a lower threshold if required by the material cutoff). This is a heuristic for
the calibrated green assets, not general image segmentation: unsupported UVs,
non-affine attributes, absent foliage or negligible savings retain the original.
Shrubs, asymmetric dry trees and horizontal cards are excluded. UVs, textures,
placement/collision bounds and instancing remain unchanged. Both replacement
quads share the existing draw, adding two triangles per affected upright card;
the same split is used for side billboards and shadows. Toggle back to original
cards with `--no-tree-card-splitting`. Coarse mip filtering can extend faint
coverage outside the cropped source silhouette, so captures must be compared
alongside timings; the four-texel guard does not cover every mip footprint.

Tree, shrub and dry-tree cards use **nearest filtering by default**: nearest
texels and nearest mip levels, retaining the existing mipmap chain. This applies
to visible cards and their sunlight/flashlight shadow alpha masks. Use
`--no-tree-nearest-filtering` to restore each asset's authored filters (normally
trilinear minification and bilinear magnification on generated cards). Wrapping,
alpha cutoffs and geometry stay the same. Town and forest volume textures retain
their authored filters. Nearest sampling can produce jagged edges and shimmer
during camera or shadow movement.

At **100 m** from the tree root to the camera in three dimensions, normal
broadleaf and pine trees switch to a **single-sided, camera-facing card**.
At elevation angles up to 45° in either direction, the front card rotates around
the world vertical axis at the root. Above 45°, the lower horizontal canopy card
rotates around its center so its normal faces the camera. Looking from below
also uses this canopy texture. Distance and angle transitions are sharp.
Shrubs and asymmetric dry trees retain their authored cards. Both scope views
and all shadow passes retain authored card orientations with double-sided rendering; collisions
and placement matrices are unchanged. Forest patches still switch to their
volume representation at the existing distance.

`--tree-billboard-distance M` changes the transition distance; `0` disables it
for comparisons. The GPU rotates the existing vertices, with at most two extra
instanced draws per species (side and top) alongside the nearby cards. Camera
culling bounds cover every rotated orientation, with nearby full-card trees
checked against their original bounds. Shadow and full-detail scope queries
retain the original bounds. No new meshes, textures or vertices are
allocated for the distant representation.

The title shows visible/total tree counts alongside the existing FPS, GPU time,
and CPU render time. These render timings include tree culling/submission and
drawing, and exclude VSync presentation. `--no-tree-culling` is useful for an
A/B comparison from the same camera position; it does not alter collision or
shadow caster culling. Use `--no-tree-shadows` to compare tree casting costs.

Inside an old-forest region, the normal view uses a sharp **100 m horizontal
cutoff** by default for vegetation, town geometry and shadow casters.
`--old-forest-distance M` changes that horizontal radius; `0` disables the cutoff.
For example, `--old-forest-distance 75` selects 75 m, and omitting the flag retains
100 m. This setting is independent of `--tree-billboard-distance` and ordinary
frustum culling. Leaving the region restores normal range.
Both scope views bypass this cutoff; returning to normal
view reapplies it. No surrounding/new forest is required. Camera membership is
spatially indexed and shared by all passes; it does not scan tree instances.
See the [old-forest visibility contract](FORESTS.md#old-forest-visibility-contract)
for geometry clipping, vertical bounds and region-authoring requirements.

Walking uses separate vertical capsule colliders for trunks, indexed
independently of camera visibility. Their height is the placement's visible
tree height. Radius is estimated as 3.5% of the smaller crown dimension,
clamped to 0.10–0.50 m and at most half the tree height. These are approximations,
since the detector does not measure trunks. The textured canopy cards never
enter the town collision world, and trees add no walkable canopy surfaces.
F3 displays nearby trunk colliders; `--no-tree-collisions` disables them.

Walking through runtime forest shrubs produces short recorded rustles. Running
is louder; a stable dry subset can also crack twigs and alert nearby zombies.
Crouching and prone movement are quieter. Stopping or leaving stops new bursts;
the current short sound finishes. These are nonblocking contact volumes, separate
from walking collision, tree rendering and explosion animation. Detection uses
a spatial index rather than scanning the forest; muting does not suppress AI
hearing. See [undergrowth audio](AUDIO.md#undergrowth-contact) for timing and tuning.

The tree layer supports static instances of the supplied double-sided,
alpha-masked impostors. There is no tree animation or occlusion culling.

Matching georeferenced maps also load [runtime forest polygons](FORESTS.md).
Beyond 350 m, woodland patches use terrain-following boxes with repeating side
and canopy textures. Nearby patches and both scope views show deterministic
individual trees calculated during scene load. Detector trees inside those
patches share their detail choice. `--vegetation` selects the polygon source;
`--no-forests` disables this layer while keeping detector trees. `--no-trees`
disables both. Placement spacing, height, textures, seed, and switch distance
are configurable in the JSON's `runtime` object.

## Mapped pole columns

Add mapped streetlights and utility poles to the published town without
regenerating terrain, buildings, or trees:

```bash
.venv/bin/python tools/prepare_pole_instances.py
# Reuse the independent pole query cache without downloading:
.venv/bin/python tools/prepare_pole_instances.py --offline
./build/godollo_viewer output/godollo.glb
```

Preparation queries the configured geographic bounding box for OSM nodes with
`highway=street_lamp`, `power=pole`, `man_made=utility_pole`, or `power=tower`.
Only mapped locations are included. A node carrying both a streetlight and
power-pole tag produces one column. Source tags, OSM IDs, attribution, query
timestamp, and map georeference are retained in the placement manifest.

Mapped objects are opaque, lit grey rectangular columns with square footprints.
Usable OSM `height` values between 0.5 and 100 meters take precedence; otherwise
these approximation defaults apply:

| Kind | Height | Width/depth |
| --- | --- | --- |
| Streetlight | 7 m | 0.20 m |
| Power pole | 9 m | 0.30 m |
| General utility pole | 8 m | 0.25 m |
| Electricity pylon | 25 m | 1.00 m |
| Approximate HÉV catenary mast (muted green) | 8 m | 0.30 m |

Preparation also reuses the town's recorded OSM extract to place a single row
of desaturated dark green HÉV masts approximately every 50 m, centered between
the two mapped main tracks. `src/hev_pole_placements.py` joins way seams and
pairs nearby parallel HÉV tracks; crossovers, yards, unelectrified tracks, and
the MÁV railway do not attract the row. Unpaired stretches receive no masts.
Their positions and dimensions are visual approximations, not surveyed poles.
The recorded railway extract must remain available in `data/raw/osm/` or at
its provenance path. These additions use manifest version 2; the viewer also
accepts older version 1 grey-only manifests.

Bases sample the published GLB's rendered terrain triangles through
`src/tree_map.py`, using runtime X east / Y up / Z south meters. The column foot
extends slightly below ground to cover slope gaps. Pylons use the same solid
column approximation; lamps do not emit light and no overhead wires are drawn.

The viewer automatically loads `poles.instances.json` beside the map. Its
version and map SHA-256 must match; an incompatible adjacent layer is skipped
with a diagnostic. Regenerate placements after replacing the map. Use
`--poles FILE` for an explicit layer (validation errors are fatal), or
`--no-poles` to disable columns, shadows, and collision together. Preparation
accepts `--config`, `--map`, `--output`, and `--refresh`; publication is atomic
and never rewrites the town GLB.

Runtime geometry is baked into 128 m X/Z cells with shared grey and green
materials and one mesh/draw per occupied cell and material. Existing
town BVH culling, sun shadows, walking collision and sight obstruction use these
batches. Pole tops are excluded from terrain-only ground queries.

## Mapped guardrails

Add only explicitly mapped OSM `barrier=guard_rail` ways without rebuilding the
town or changing the fingerprints of its tree and pole layers:

```bash
.venv/bin/python tools/prepare_guardrails.py
# Subsequent preparation can reuse the separate guardrail extract:
.venv/bin/python tools/prepare_guardrails.py --offline
./build/godollo_viewer output/godollo.glb
```

The viewer automatically loads `guardrails.glb` beside the town. The sidecar
embeds its texture, source attribution, OSM IDs, map SHA-256, and versioned
metadata. Stale adjacent layers are skipped with a diagnostic. `--guardrails
FILE` selects a layer explicitly and treats validation failures as errors;
`--no-guardrails` disables its rendering, shadows and collision. Preparation
supports `--config`, `--map`, `--output`, and `--refresh`, and validates the
exported float32 geometry before atomic publication.

Geometry is a vertical double-sided strip, using two triangles per straight
section. Additional sections retain bends, terrain slope changes and 128 m
batch boundaries. Both fences and guardrails use `src/terrain_profile.py` to
follow the published terrain triangles. All geometry is exported once from
local east/north/up to runtime X east / Y up / Z south meters.

The rail and posts are painted into the shared
[source alpha texture](../assets/guardrails/README.md); posts have no additional
geometry. The default height is 0.75 m with an 8 cm textured post every 2 m.
Usable OSM heights from 0.3 to 2 m override the default. Opaque texels write depth
and cast shadows; transparent texels are discarded. Spatial batches use the
existing town BVH culling and collision. Walking and sight use the continuous
strip, including transparent space beneath the beam, as with existing masked
fences. Mapped breaks remain actual gaps in both rendering and collision.

## Town visibility

The viewer also culls town draw instances outside the camera frustum by default.
It indexes conservative full transformed bounds for terrain, roads, walls, and
roofs; the `Town V/T` title values are visible and total draw instances, not a
count of physical buildings. Use `--no-town-culling` for a same-camera draw
comparison. This does not affect collision. Buildings retain their facade detail
LoD; there is no town occlusion culling.

```bash
./build/godollo_viewer output/godollo.glb --fps --no-town-culling
```

Static opaque roofs are merged at load time by material and 128 m world X/Z
cell. The existing source-object BVH selects cells independently for the camera
and sun shadow passes. A selected cell draws all its roofs, trading a small amount
of conservative overdraw for fewer draw calls, without rebuilding GPU buffers
each frame. `Town V/T` still counts source objects passing visibility checks;
the title also shows actual color-pass draws, which are printed on exit. Roof
transforms, normals, texture coordinates, vertex colors, and reflected winding
are baked once.
Doors, facade detail alternatives, transparent surfaces, and collision keep
their original geometry and behavior. No map regeneration is needed.

Simple static opaque building walls also merge by material and 128 m cell.
For camera and flashlight passes, a cell uses its merged mesh only when every
building in that batch is beyond its facade detail distance. This conservative
check uses the union of the complete alternative-group bounds (including moving
door padding), the largest member distance, and the current projection's scope
magnification. Cells containing nearby buildings use the individual source
draws, so simple walls cannot fill detailed door or window openings, even when
the nearby member is offscreen. Sun shadows retain their existing simple-shell
policy and may batch these walls at any distance.

Eligibility is checked once per selected cell per pass; no town population scan
or geometry upload is needed each frame. Camera, sun and flashlight reset their
batch selections independently. The spatial BVH and old-forest range clipping
still use the original sources. As with roofs, drawing a complete cell may submit
some extra geometry outside the view, and merged GPU meshes require extra storage.

Use `--no-roof-batching` to compare the same view with individual roof draws:

```bash
./build/godollo_viewer output/godollo.glb --no-roof-batching
```

Use `--no-wall-batching` for the corresponding wall comparison. The two switches
are independent; source-object visibility and collision remain unchanged.

## Zombie crowd

The [zombie assets](../assets/zombies/README.md) live separately from the generated
map in `assets/zombies/models/`. With these files installed, opening terrain at
least 100 m wide and deep automatically places **300 zombies evenly across the
Grassalkovich Royal Palace gardens** (Felső park / Kastélypark) when the published
map covers them. The sourced park outline is converted using the map's recorded
projected origin in EPSG:32634, so changing `--spawn X Z` does not move the crowd.
Other scenes retain the 1,000-zombie crowd near the map center or spawn. The small
built-in test scene does not automatically load the crowd.

```bash
./build/godollo_viewer output/godollo.glb --fps
./build/godollo_viewer output/godollo.glb --zombie-view
./build/godollo_viewer output/godollo.glb --no-zombies
./build/godollo_viewer output/godollo.glb --zombie-count 1000 --zombie-radius 150
```

`--zombies PATH` selects one animated GLB/glTF or a directory of individual
characters. Directory files are sorted and assigned evenly to the crowd. Each
character needs an animation named `idle` (preferred), an exporter-prefixed
terminal `idle`, or a single clip whose name contains `idle`. Unsupported
animation inputs fail with a diagnostic instead of displaying a static pose.
Character height is normalized to 1.8 m and the animation's lowest point is
placed on the rendered ground surface. Park placement selects well-separated
positions across the whole outline from a deterministic safe-ground grid, with
randomized facing. It keeps at least 2 m between centers and rejects steep ground,
buildings, and tree trunks. `--zombie-count N` overrides the population.
`--zombie-radius M` selects the original radial crowd near the map center or spawn,
using a jittered grid with a 2 m center clearing; its default radius is 100 m.
Insufficient safe ground reports an error with the accepted count. Increase the
radial radius or reduce the count for a smaller or densely built scene.

Idle, walk, and run deformation is sampled once at startup at 30 samples per second.
Shared vertex animation buffers hold the resulting positions and normals; the
GPU interpolates adjacent samples and loops using elapsed time. Every zombie
has its own phase offset. Meshes, textures, and animation samples are shared by
all instances of a variant, with one instanced draw per character primitive.
The title reports visible/total zombie counts, and shutdown logs the visible
count and instanced draw count. Camera-frustum culling compacts each character
batch before upload and drawing. Bounds include every idle/walk/run sample and
live ragdoll bone transforms, so animated limbs crossing the view edge remain
visible. Mesh-local bind bounds (possibly centimeters) are transformed through
the ragdoll palette before use; living bounds use scene-space baked frames.
Sun shadow casters use the light frustum independently: an offscreen
zombie can still cast a visible shadow. `--no-zombie-culling` disables only
camera-frustum culling for a same-camera comparison; distance settings remain
active. AI, shooting, and physics continue for the full crowd regardless of
render visibility.

Generate lower-detail assets once using the commands in
[the zombie asset guide](../assets/zombies/README.md#reduced-detail-meshes).
The viewer discovers the hash-checked `lods.json` alongside the original models.
At the normal 60-degree vertical field of view, the automatic defaults are:

| Camera distance | Mesh | Animation |
| --- | --- | --- |
| Below 12 m | Original | Continuous interpolation of 30 Hz baked poses |
| 12–35 m | LOD1, about 50% triangles | About 15 Hz, single pose |
| 35–80 m | LOD2, about 20% triangles | About 10 Hz, single pose |
| 80–180 m | LOD3, about 7% triangles | About 5 Hz, single pose |
| Beyond 180 m | Camera draw omitted | Simulation continues |

Distances use the conservative animated world-bounds center. Zoom scales these
thresholds with projected size, keeping original detail for distant scoped
targets and extending the camera cutoff. Animation steps snap to the existing
baked frames on a lower-rate cycle grid; they avoid interpolation and the second
pose's texture fetches. Near playback remains smooth. Stored animation clips
are preserved at every LOD; this is a rendering rate reduction, not removal of
animation data. The renderer publishes the exact camera frame/clip for head hit
tests and ragdoll handoff, so a shot uses the displayed stepped pose.

Sun shadows use LOD3 independently of camera detail and camera distance cutoff.
An offscreen or distance-culled zombie can still cast into the existing light
volume. Camera-visible casters use the camera pose; other casters use a single
baked pose. Shadow silhouettes are deliberately coarser. A ragdoll's camera and
shadow meshes share one persistent bone-palette cache, including settled bodies.
Bounds and simulation continue to use the authoritative original asset.

`--zombie-draw-distance M` changes the normal-FOV cutoff; `0` disables distance
removal while retaining LOD and animation stepping. `--zombie-view` defaults to
no distance cutoff, so its overview can show the entire crowd; an explicit
`--zombie-draw-distance` still takes precedence. `--no-zombie-lod` disables mesh
selection, animation stepping and the distance cutoff for comparison.
`--no-zombie-shadows` disables only zombie casting for GPU cost comparisons.
`--profile` prints the existing asynchronous GPU and CPU render timings once per
second, including during hidden runs. The title also reports `CPU sim` for
physics, camera movement and zombie behavior before rendering. Console profiling
separates behavior time and renderer preparation (door/instance/bounds updates),
and reports the behavior-pool entry count and the last step's perception/sight-ray
counts. Pool entries track perception/behavior activity, not the total number of
scenario travelers.
CPU render time covers submission; hidden-run FPS omits presentation/VSync waits. Shutdown
also reports camera LOD counts, submitted camera/shadow triangles and the number
of frustum-visible zombies rejected by distance.

For a repeatable 10,000-zombie overview comparison:

```bash
./build/godollo_viewer output/godollo.glb --zombie-count 10000 --zombie-radius 300 \
  --zombie-view --zombie-draw-distance 0 --hidden --frames 300 --profile --day-length 0 \
  --animation-time 0.5
# Repeat with --no-zombie-lod for original geometry and continuous playback.
```

Zombie rendering first culls groups in a renderer-owned 20 m X/Z grid, shared
across character variants. An instance belongs to one cell according to the
center of its full world-space rendering bounds. Each group's bounding box
encloses all members' complete boxes, including animation, fallen ragdolls,
and geometry extending beyond the cell. A group outside the frustum skips all
members; a group wholly inside accepts all members; only boundary groups need
individual tests. Camera, sunlight, and flashlight classification are independent, retaining
offscreen shadow casters. Results retain original instance-ID order before asset
batching, so draw ordering and palette assignment remain stable.

`Renderer::updateZombies()` marks groups dirty when an instance's bounds change,
moves membership across cell boundaries, and removes corpse tombstones. Before
the next camera/shadow query, dirty groups rebuild their aggregate boxes once,
including shrinking after removal or a pose change. Empty group slots are reused
without retaining historical cells in the query. Animation time alone does not
dirty living bounds, which already cover every baked clip sample; unchanged
settled corpses also retain their groups. Camera movement still queries the
groups every frame, independently of instance preparation.

Instance preparation is incremental. `ZombieLayer` publishes deduplicated changed
IDs, and `Renderer::updateZombies()` refreshes only those entries after the initial
full upload. Locomotion, turns, animation-state/time changes, shooting, physics
pose changes, external palettes, and corpse removal notify automatically.
Transform or pose changes update world bounds; animation-only changes refresh
instance data without recalculating bounds. Idle playback still advances from
render time, and each frame still selects camera/shadow instances, LODs, and
uploads the same compact instance buffers. This reduces CPU preparation work;
it does not reduce the geometry or shader work on the GPU.

Direct edits to public instance fields require `markRenderChanged(id)` before
the next renderer update. Its default also refreshes bounds; use
`markRenderChanged(id, false)` only for animation state, phase, or clip time.
After bulk replacement use `invalidateRenderData()`, or `rebuildSpatialIndex()`
when gameplay indices also need rebuilding. Population and asset topology stay
fixed after renderer creation. A full refresh also resets retained palette slots,
so an explicitly replaced pose cannot inherit stale GPU data.

Each renderer retains its own publication cursor. Changes are retired only after
successful preparation; a consumer that missed a retired batch performs a full
refresh. Replacing a layer at the same address also invalidates cached data.
All operations run synchronously on the existing thread. `--profile` reports
the last update's instance and bounds counts, including full versus incremental
refreshes, through `Renderer::zombieUpdateStats()`.

The group index borrows the existing per-instance bound boxes; their vectors
and asset/population topology must remain fixed after upload. Changed boxes must
be reported to the index through `update()`, and removed IDs through `erase()`;
the renderer centralizes both in `updateZombies()`. It refits before publishing
visibility on the existing render thread, adding no concurrency. Additional
storage is CPU-only membership, group boxes/member IDs, batch lookup, and query
results. Group slots and query capacities can remain allocated for reuse;
empty member buffers are released, and oversized member buffers shrink during
refit after a crowd disperses. This adds no GPU resources. `Renderer::zombieVisibilityStats()` reports
occupied/refitted groups and camera/light group and individual test counts for
the last rendered frame. `--no-zombie-culling` bypasses camera group/individual
tests while retaining shadow culling.

Ragdoll bone palettes are uploaded for the union of camera-visible,
sun-shadow-casting, and flashlight-shadow-casting instances before the draw
passes. A corpse needed by several passes uploads once. The light volumes remain
conservative, so they can include casters whose shadows ultimately do not affect
visible pixels. Disabled shadows and nighttime do not request sunlight caster
palettes.

Each asset batch lazily assigns persistent GPU palette slots. Unchanged poses
remain in their slots, including hidden corpses; changes outside all active
frusta wait until the corpse is needed again. Adjacent dirty slots are combined
into one buffer update. Storage grows geometrically and copies existing slots
on the GPU, preserving hidden settled palettes. Removal releases a slot for
reuse, which always requires an initial upload. Allocated storage remains until
renderer destruction; it uses 64 bytes per bone per allocated slot, with spare
capacity from growth, subject to the GPU's texture-buffer limit.

`PhysicsWorld` consumes body movement events after **every** fixed step,
including the final pose when a body sleeps. `ZombieLayer::synchronizePhysics()`
rebuilds a corpse's skinning palette only when its physics revision changes.
Ordinary collisions and impulses that wake a corpse therefore invalidate it
automatically. These operations and GPU updates run synchronously on the
existing simulation/render thread; they add no concurrency. Rendering still
streams compact instance transforms for each pass. `Renderer::zombiePaletteStats()`
reports the last frame's needed/resident palettes, uploaded palettes, update
calls/bytes, and retained GPU storage bytes.

There is a small maintenance contract: **changes made outside normal physics
simulation must go through the ragdoll's owning API**. This includes:

- Teleporting or resetting a corpse: use `ZombieLayer::translateRagdoll()` for
  translation. Any future reset API must update physics and invalidate the pose
  revision together; direct body-transform or palette edits bypass the cache.
- Replacing its skeleton: perform replacement through an owning API that
  invalidates the palette and bounds. Asset skeletons, weights, and population
  topology are currently fixed after renderer creation; replacing a rig requires
  rebuilding the renderer's asset buffers and palette slots, even if bone counts
  match. This change does not add live skeleton replacement.
- Destroying a corpse and reusing its GPU slot: use
  `ZombieLayer::removeRagdoll()`. It destroys/unregisters physics bodies and leaves
  a tombstone at the stable instance ID. The next `Renderer::updateZombies()`
  releases its slot; reuse never inherits the previous owner's palette revision.

`ZombieLayer::applyRagdollImpulse()` is the owning entry point for external
forces. `setRagdollPalette()` supports externally driven rigs and fixtures,
validates the palette and advances its revision, and rejects physics-owned or
removed corpses. Initial placement/fixture construction may populate instance
fields directly, followed by `rebuildSpatialIndex()` to rebuild shooting,
activation, and corpse membership. Subsequent ragdoll changes must respect these
APIs. After simulation or owning API changes, call `Renderer::updateZombies()` before
`render()`, as the application already does. The renderer borrows the most recent
layer for palette data, so that layer must remain alive and unchanged throughout
rendering. Corpse removal is an API facility; no automatic removal policy is added.

`ZombieLayer` maintains a dense, unordered list of corpse IDs exposed through
`ragdollIds()`. Shooting and external palette activation add IDs once; removal
swap-removes them without moving the actual instances. Door activation around
corpses and physics synchronization iterate this list, while `ragdollCount()`
reads its size. Their work scales with the number of corpses, rather than the
entire zombie population. Sleeping corpses remain listed; synchronization retains
the existing pose-revision check, so unchanged poses still skip reconstruction.
The list is updated synchronously and adds no concurrency. Its borrowed view is
invalidated by corpse creation/removal or an explicit index rebuild.

Living zombies normally play idle. Within **100 m**, including the boundary,
each receives an initially idle behavior object. Distance uses player feet in
3D; free-fly supplies a virtual body with its eyes at the camera. The dense
behavior vector has a hash map from instance IDs to slots for fast membership
and lookup. Removal moves the last member into the vacated slot. Instance IDs
are stable indices in the crowd; the instance vector must not be reordered.

Activation and hearing query a 20 m X/Z spatial grid, then apply the original
3D distance and eligibility checks. Locomotion updates cell membership before
noise handling; dead zombies leave the grid. External code that edits instance
feet/transforms, instance normalization, or asset skeleton/clip data directly
must call `ZombieLayer::rebuildSpatialIndex()`. It refreshes both grid membership
and prepared shooting geometry. Changing the population or asset count triggers
a rebuild automatically.

Only members of this pool perform perception. A **100-degree horizontal FOV**
uses the character's forward direction (+Z), ignoring height for the cone test.
Inside the cone, three finite rays run from zombie eye height (1.65 m) to player
head, torso, and feet. The scene and trunk collision BVHs test occlusion by
double-sided triangle surfaces and trunk capsules; canopy cards and other
zombies do not block sight. Scene geometry and trunks are checked before live
door panels: already-blocked long rays skip the door query over the endpoints'
bounding rectangle. This preserves occlusion while avoiding large door-candidate
lists when free-fly takes the perceived player far from activated zombies.
One or two clear rays store the current player feet
and change idle or looking to **investigating**. Three clear rays store the same target and
change idle, looking, or investigating to **chasing**. Partial sight refreshes a chasing
zombie's target without downgrading its state.

Sight observations run at **10 Hz within 10 m**, **5 Hz over 10 m through 30 m**,
and **1 Hz beyond 30 m**, using 3D distance between feet. Activation gets an
immediate first observation; later checks use deterministic offsets from stable
zombie IDs to spread work across frames. Moving into a faster band shortens the
next deadline. A late frame performs one check rather than replaying missed
observations. Everything is polled synchronously on the existing simulation
thread using `SimulationClock`; there are no worker threads, background tasks,
or timer callbacks.

Perception has a separate **100/150 m** activation/suspension boundary, using
3D distance between feet in both walking and free-fly. An active entry keeps
observing through 150 m, including that boundary. Beyond 150 m it suspends all
sight checks and clears cached visibility, retaining its last seen target and
pursuit state. Investigating/chasing zombies continue terrain-following movement
and their walk/run animation toward that target, without tracking the current
player or stopping short based on a stale clear view. Looking zombies finish
their existing turn. Idle entries beyond 150 m are evicted immediately, including
entries that become idle on arrival or after finishing a turn in the current step.

A suspended entry stays suspended while returning through the 100–150 m band.
On the first update within 100 m, including the boundary, its perception clock
resets and performs an immediate observation, then returns to the normal
10/5/1 Hz schedule. The gap avoids repeated suspend/resume transitions near one
boundary. Evicted living zombies remain in the spatial index and can receive a
fresh idle entry with immediate sight when the player returns within 100 m.
Range suspension also applies with `--no-zombie-staggering`; that option changes
observation cadence, not activation/suspension distances. No concurrency or
ragdoll-physics policy is added. Distant pursuers still incur movement and terrain
query costs until arrival; their sight queries are skipped entirely.

Between observations, zombies retain the last observed visibility and target;
cached sight never copies the player's current position into that target.
Movement, looking turns, the cheap attack-distance guard, and idle expiration
still run every simulation step. Attack visibility has its own fixed 10 Hz
schedule within 1.5 m. Hearing remains immediate and event-driven. Sight can
take up to roughly 100/200/1000 ms plus frame scheduling to react, and brief
glimpses between checks may be missed. `--no-zombie-staggering` restores
every-step sight checks for comparison. Rays stop once partial visibility is
known; every-step chasing can stop at the first clear ray.

`--no-zombie-perception` disables the perception-driven behavior system entirely:
no nearby activation scans, behavior entries, sight/hearing queries, pursuit or
attacks. Ordinary zombies remain idle and still render, take damage and enter
ragdoll physics. Authored scenario routes continue independently, including
their movement/crowd collision work. The disabled ordinary-population update
returns before spatial activation queries or behavior-pool traversal; noise
events also return before any spatial query. The owning API is
`ZombieLayer::setPerceptionEnabled(false)`, which also clears existing behaviors
and stops their pursuit if changed after startup. Reenabling allows fresh
activation. The same flag is available on the multiplayer server; clients follow
the server's simulation.

`ZombieLayer::makeNoise(position, decibels)` creates a silent simulation noise
at an X/Y/Z position. Source levels are dB at **one meter**. Perceived level is
`source dB - 20 * log10(max(distance in meters, 1))`, using 3D distance. This is
a simple game model: walls, vegetation, and materials do not attenuate noise.
The tunable levels in `src/noise.hpp` are **40 dB walking**, **50 dB running**,
and **55 dB landing**, with a **20 dB hearing threshold**. These make walking
audible at 10 m, running at roughly 32 m, and landing at roughly 56 m.

Only idle, living members of the behavior pool respond to audible noise. They
store its horizontal direction and enter **looking**, turning in place along
the shortest yaw arc at **90 degrees/s** (180 degrees in two seconds) while
playing idle. Further noises do not redirect a looking zombie. Visibility can
interrupt the turn and start investigation or chasing. Finishing the turn
without seeing the player returns to idle with a fresh ten-second timer; noise
alone does not give a zombie a movement target. A source at the same horizontal
position has no direction to turn toward and is ignored.

Grounded player movement emits **1.5 walking footsteps/s** or **3 running
footsteps/s**. These events come from actual displacement in fixed physics
steps, keeping cadence independent of rendering frame rate. Standing still,
being blocked by a wall, jumping/falling, and free-fly movement do not produce
footsteps. A transition from airborne to grounded emits one landing noise,
including after a jump or fall. Spawning or switching into walking mode does
not emit landing noise. The same actions also drive footsteps/landing playback
through the separate [audio layer](AUDIO.md); muting or unavailable audio never
changes these perception events.

Investigating zombies walk at **1.2 m/s**; chasing zombies run at **3 m/s** with
faster strides. When sight is lost, they continue to the last seen position and
return to idle on arrival. Every return to idle starts a fresh **10-second**
countdown, including initial creation. Within 150 m, remaining idle for that
interval removes the behavior object; beyond 150 m, idle eviction is immediate.
Leaving idle cancels the countdown. A nearby zombie can receive a new idle
object on the step after expiration. Active zombies within **1.5 m** of the
player enter **attacking** only when the player is in the normal horizontal FOV
and a fresh ray to the head/eye height, middle of the body or feet is clear.
The check stops at the first clear point, using at most three rays against
current walls, doors and props. They stop moving and currently play idle without
dealing damage.

Attack eligibility is sampled at **10 Hz**, with stable-ID phases in simulation
time, independently of normal perception and `--no-zombie-staggering`. Entering
or reentering range checks immediately, then follows those slots; missed slots
never cause a catch-up burst. Leaving range clears eligibility immediately
without rays. Closing a door can take up to 100 ms plus frame scheduling to
leave the attack state. A newly blocked attack observation invalidates stale
clear sight while preserving the last seen target. Reopening restores eligibility
on a subsequent scheduled check. `ZombieBehaviorStats::attackChecks/attackRays`
report this work separately from ordinary perception.

`canZombieAttack()` in `zombie_attack` always checks current range, FOV and
visibility without using either cache. Future damage application must call it
at the actual hit; an attacking state or cached eligibility alone does not
authorize damage. It also rejects removed zombies and ragdolls. No zombie damage
or attack-animation system exists yet.

`SimulationClock::instance()` provides elapsed in-game seconds throughout the
viewer. The app advances it once per simulation step using the same capped
delta as movement/physics. Minimized frames do not advance it. Animation uses
this clock too; `--animation-time` overrides visual animation time while AI and
physics continue simulating.

Locomotion faces the direction of travel and follows rendered terrain triangles.
Living zombies use **0.30 m radius, 1.8 m tall upright body cylinders** for cheap
crowd collision. Their centers stay at least 0.60 m apart on the same level;
animated arms and other extremities can still intersect. Movement sweeps the
whole proposed segment, stops at a body, and slides along an oblique contact.
Head-on queues can stop without choosing a route around the crowd. Idle and
inactive living zombies also block movement; ragdolls and removed bodies do not.
Characters on vertically separate floors do not block each other. Blocked
pursuers keep their target and stop advancing their locomotion animation.

`ZombieCrowd` maintains a separate **1 m X/Z spatial grid**, updated incrementally
alongside the perception/shooting index. Only active behavior IDs are sorted and
stepped, in stable ID order, so pool swap removal and hash iteration do not
choose collision priority. Given the same initial data and simulation steps,
movement is repeatable; different frame timesteps need not produce identical
crowd paths. Each crowd segment allows up to three sliding contacts, with **64
neighbor checks total across all segments of one zombie's movement**.
Exhausting that budget stops at the last verified position
instead of skipping unchecked bodies. This bounds dense-cell neighbor work,
including malformed piles, without rigid bodies or all-pairs crowd checks.
`--profile` reports neighbor checks and budget stops; `ZombieLayer::crowdStats()`
exposes those counters. The existing deterministic spawn placement starts bodies
well apart. This constraint preserves that separation; it does not untangle
arbitrarily overlapping spawn data, although outward escape is allowed.

Moving zombies also collide with **walls, current door panels, interior boxes,
pole columns, fences, guardrails, and tree trunks**, using the same accelerated
`CollisionWorld` queries as player walking. The world body is a **0.30 m radius,
1.8 m tall upright capsule**. Movement uses segments no longer than **0.10 m**
and at most four contact corrections per segment; normal app timesteps need one
or two segments. A hard 2,048-segment limit stops excess displacement from huge
external timesteps. Stable contact tie-breaking, bounded correction, and retaining
the last safe position keep tight corners deterministic. Glancing wall/jamb
corrections are shortened to the allowed travel and checked again, allowing
sliding without extra speed or repeated rejection at a clear doorway. A crowd
slide is checked against the world again before acceptance, so corrections cannot move a body
through another zombie or a wall. Only moving zombies query obstacles.

Zombies keep an additional **0.05 m clearance from steep building wall faces**
on either side, including doorway jambs. This leaves room for animated limbs
without changing wall meshes, rendering cost or the 0.30 m crowd body radius.
The same margin applies during step-up and crowd-slide validation. Player
collision, horizontal floors/foundation caps, door panels and other props retain
their existing clearance. A 1 m doorway still accommodates the 0.60 m body plus
the two 0.05 m wall margins.

Zombies and grounded players can step onto walkable surfaces up to **0.30 m**
higher, including road overlays, low curbs/props, and exposed building foundation
caps. `character_step` supplies the shared lift/forward/lower check. It probes
within the rounded foot's forward radius so a raised edge can be found before
the feet center crosses it, checks headroom throughout the lift, and settles
onto capsule support without adding horizontal speed. Steeper surfaces, taller
steps, low ceilings and intervening walls remain blocking. Ordinary movement
uses its existing collision path; the extra step checks run when that motion
is obstructed. Zombie crowd checks still share one budget across the movement.

Walking support includes the upper side of downward-facing foundation caps
within the permitted height range, without changing their mesh or winding.
`CollisionWorld::walkingGroundAt()` provides that query; ordinary `groundAt()`
and terrain-only placement retain their previous cap exclusion. Ceiling and
roof undersides are not reclassified as floors. Road clearance, map geometry,
and the generator configuration are unchanged.

Doors and boxes block at their current physical poses without zombie forces or
impulses. Opening a door reveals the existing doorway without rebuilding the
collision world. Living zombies near activated door sweeps also have lightweight
kinematic capsules in the shared physics world. Door forces still apply normally;
the existing hinge/contact solver stops the panel against a zombie instead of
swinging through it. This includes idle zombies. An outward-opening door may
remain blocked until the zombie clears its swing path.

`ZombieDoorCollision::prepare()` runs after door activation and before physics,
using the existing zombie spatial index. Only zombies near activated door sweeps
receive proxies; stable ID order and shared proxies for adjacent doors keep
updates deterministic. Proxies follow accepted zombie feet and are removed when
the zombie leaves, dies or is removed. They contact door panels only, with a 2 cm
numerical pad outside the movement body for contact slop and impulse compression.
They do not make zombies dynamic bodies or add collisions with boxes/player/ragdolls.

The rendered fence/guardrail panels are solid for movement,
including transparent texels, while actual mesh entrance gaps remain open.
`--no-poles`, `--no-fences`, `--no-guardrails` and `--no-tree-collisions` also remove
those respective zombie obstacles. Pole/guardrail sidecar fingerprint checks
remain the same as for rendering and player collision.

Zombies do not yet find routes around buildings or crowds, push/open doors,
push boxes, or collide with the player. Standing zombies have no dynamic physics
response: doors stop against them, while other moving props do not push them.
The movement module keeps scene constraints, crowd constraints, terrain sampling and transform publication
separate for later navigation and player interaction work.
Outside the map zombies retain their last ground height. Walking and running play
the creator's walk/run clips using shared baked vertex animation. Investigating
uses walk; chasing uses run, falling back to walk when only that clip is available.
Custom idle-only models retain idle playback while moving. Tiny collision slides
retain facing, including backward/sideways slides under 2 cm that achieve less
than half the requested movement. Larger locomotion turns follow the shortest
arc at up to 180 degrees/s. Animation starts above 0.20 m/s and continues down to
0.10 m/s of forward progress. Brief obstruction holds the current gait pose;
after 0.15 s it settles into idle, with a 0.15 s restart hold to avoid rapid
idle/run switching. Explicit idle, looking and attack transitions stop immediately.
Locomotion clip time advances by actual travel time during meaningful movement;
the CPU does not evaluate or upload a skeleton for each moving zombie. These
presentation rules leave accepted collision positions unchanged.
Distance LOD affects rendering and displayed poses; zombies
still have no occlusion culling.

In Combat mode while the mouse is captured, a weapon shot casts one ray per
pellet from the camera center and damages the nearest living zombie along
each ray. Prepared characters use `NAME.capsules.json` beside the original GLB:
bone-attached capsules fitted once to the reference mesh, with a robust near-full
surface fit that avoids inflating for a few protruding vertices. Skirted Female A/E
use estimated underlying pelvis/thigh anatomy. Unused helper bones have disabled
records. The loader validates source fingerprints; the capsules follow the
displayed idle, walk or run pose, with head descendants classified as headshots.
Shooting first queries the same spatial grid along the forward ray. The query
tests occupied X/Z cells expanded by the maximum hit-region reach, avoiding
arbitrarily long walks through empty space. It then rejects candidates with
cheap 3D bounds covering all hit regions through every animation
sample and interpolated frame, including wrap. Only remaining candidates sample
their skeleton and intersect the posed bone capsules, including limbs, fingers
and feet. The nearest surface determines ordinary damage and world occlusion.
Queries have no shooting
distance limit or camera-visibility dependency; equal-distance hits retain
original instance-ID order. Conservative roundoff padding preserves the existing
hit decisions on long rays. Capsule rays preserve world distances through
instance/bone scale, reflection and shear; rays starting inside return zero.

Custom assets without capsule definitions retain their original body/head
sphere fallback. For this fallback, head vertex membership is prepared once per asset at grid initialization:
vertices with at least 50% combined weight on the head or descendant bones are
retained, including hair and all eight skin influences. Detailed head queries
pose only those vertices using their complete weights. The original body/head
spheres and aiming allowances remain unchanged, so a body miss can still be a
headshot. Locomotion updates shooting bounds along with cell membership, without
sampling a skeleton. `ZombieLayer::shootingStats()` reports candidate count,
sampled poses, and posed head vertices for the last shot. Everything runs on the
existing simulation thread.

Unmapped fallback rigs use a standard head region. Each zombie starts at 10 HP. The
`ZombieDamage` class stores a record by stable zombie ID only after a confirmed,
unblocked hit. Low-caliber bullets/pellets deal 1 damage; high caliber deals 2.
Headshots double those values. If multiple damage volumes intersect the ray, the first
surface entered determines the region. Each shotgun pellet applies damage
separately, including repeated hits on the same zombie. Surviving zombies retain
their behavior, navigation and living collision membership. At zero HP the layer
activates the ragdoll and erases its injury record. Grenade deaths also erase
existing injury records; their blast behavior is unchanged.

Prepared torso surfaces receive geometric wounds. After the usual spatial-grid
and bounding-box checks, torso candidates use the remaining posed torso
triangles to place a shot on the visible surface. Each hit removes every
triangle incident to a torso vertex within **10 cm in world space** of that
point. This is a vertex-distance rule: a large triangle can disappear entirely,
or remain intact if none of its vertices is close enough. Repeated hits add to
the same persistent wound mask; subsequent shots can pass through an opening
and hit an intact back surface. Other body regions retain their capsule tests,
and joint-hit precedence remains unchanged. Corpses retain their existing
non-targetable shooting behavior.

Wound state is separate from health and survives ragdoll activation. The camera
fragment shader discards those exact original triangle IDs, including their
depth writes. Wounded camera instances use the original mesh so LOD changes
cannot close a hole. Prepared severed meshes match wounded reference vertices
when selecting their visual triangle mask. **Sun and flashlight shadow casting
keep their existing geometry and LODs**, without wound removal or shadows from
the placeholder contents.

The intact torso's anatomical boundary loops are closed before damage is
applied. Camera-only dark red cap triangles follow their original rim vertices
through baked animation and live ragdoll skinning. Shots remove incident cap
triangles using the same vertex mask as the outer surface and inner collider;
new bullet openings are never capped afterwards. Surviving torso triangles
are double-sided for wounded actors, with dark red inward-facing surfaces.
These closures are prepared from torso boundaries independently of the
precomputed dismemberment caps. Cap GPU resources are shared per mesh variant
and allocated on its first wound; intact crowds retain hardware backface culling.

The first wound creates an inward-facing torso collider, contracted to 80% of
the reference surface around its center. Its fixed shape follows the torso's
rigid animation/ragdoll frame. Anatomical boundary loops are capped; shot
openings are left open. The collider is partitioned below Box3D's per-contact
triangle-cache limit and rebuilt only when damage changes. Three small dark red
boxes are created inside as loose demonstration contents. They collide with
their own cavity and streamed terrain/building surfaces, and ignore the solid
ragdoll capsules. Physics determines whether they fit through an opening.
The shell is an approximate fit during animation; rapid motion also inherits
Box3D's moving-mesh continuous-collision limitations. Removing a corpse releases
its cavity, objects, wound record and GPU mask slot. Unwounded actors do not
create these extra physics bodies. Multiplayer snapshots carry packed vertex
masks and authoritative placeholder poses; clients do not simulate the contents.

Accepted bullet hits emit one camera-facing blood-mist quad: ordinary hits use
a 36 cm burst, dismemberment uses 70 cm, and shots that open more torso geometry
use 110 cm. The quad expands slightly and fades over one second. All sizes share
one small cloud-mask texture generated at first use, with per-hit rotation and
an ambient-tinted dark red color. A bounded 128-event history feeds one sorted,
instanced draw with depth testing, alpha blending and no depth writes. There is
no particle physics, shadow casting or per-burst lighting pass. Misses and
occluded shots emit nothing; shotgun pellets retain separate hit bursts.
Multiplayer carries event IDs, positions, sizes and ages, retaining 1.8 seconds
of history to cover interpolation and packet loss without restarting the fade.

The standalone viewer also emits ten small ballistic blood droplets per hit.
Dismemberment activates a wound-attached emitter at 60 droplets/second for one
second. The fixed pools hold 1,024 droplets, 16 emitters and 256 floor stains;
full particle/emitter pools skip excess emission, and stains replace the oldest
slots. Particle and emitter updates use no heap allocations or physics bodies.
Launch position, velocity and age drive gravity in the vertex shader. Droplets,
stains and the original mist share a texture and a sorted instanced draw;
render scratch storage and the instance buffer have fixed capacity.

Each hit queries static ground/floor once, excluding movable boxes, and caches
a local plane. Predicted landings retire droplets and leave randomly rotated
splats aligned to that plane, offset 8 mm to reduce depth fighting. Stains fade
over their final three seconds and expire after twenty seconds. This deliberately
approximates uneven terrain, stairs and ledges; spray ignores walls and moving
objects. Emitters retain their initial floor plane while following the ragdoll.
There are no wall stains, moving-object attachments or blood shadow passes.
The new droplets/emitters/stains are local to the standalone viewer; multiplayer
continues to replicate the existing mist events without a protocol change.

Prepared characters also load `NAME.hitboxes.json` beside their original GLB.
These spherical colliders follow the authored waist, neck, shoulders, elbows,
hips and knees. A joint hit overrides the ordinary damage volumes on
that actor and checks world occlusion to the joint collider. It immediately
kills regardless of HP, removes the health injury record, selects a precomputed
separation mesh with dark red caps, and releases the matching ragdoll constraint.
Joint dismemberment uses its colliders and prepared meshes. The hit physics body
receives a random impulse along the shot direction: 4–8 N·s for low caliber or
10–16 N·s for high caliber. Non-head parts also receive an equally strong impulse
away from the posed pelvis center toward the severed body's center; the two
impulses add without renormalizing. Heads keep shot-direction-only launch.
Distal sections follow through their joints, so larger
assemblies tend to move more slowly. The hit body also gets a random spin axis
at 1–4 radians/second; connected sections respond through their remaining joints.
Rolling resistance slows the capsule/sphere colliders on contact, and linear
drag of 1.5/s on the severed hit body limits travel across its local floor.
The remaining body and detached pieces use the original shared bone palette
and physics bodies. Custom assets without these definitions keep
the body/head damage path. See [prepared zombie assets](../assets/zombies/README.md#joint-hits-and-prepared-severed-meshes).
Box3D then simulates an 11-body capsule/sphere ragdoll with
constrained ball and hinge joints. Idle, walking, and running instances use shared
baked vertex-texture animation; only ragdolls use live GPU skinning with retained
bind-pose weights. Activation stops pursuit and samples the displayed idle, walk,
or run skeleton on demand, capturing its pose, including each zombie's phase and
interpolated animation frame. Bodies start in that pose, with their dimensions
and joint axes derived from the reference rig;
hands, fingers, and other unmapped joints retain their captured local poses.
At activation, a second skeleton sample 1/30 second earlier in the same clip
estimates a separate linear velocity for each physics body's center. The sample
follows the displayed clip/frame when available, including wrapping across the
clip boundary. Estimates use world meters/second and are capped at 20 m/s to
limit animation discontinuities. This runs only at death, for health damage,
grenade deaths and dismemberment, and adds to the existing shot/blast velocity.
Every body also inherits the instance's actual world-space travel velocity from
the last accepted movement step, including walking/running, terrain height
changes and collision/crowd sliding. Blocked actors inherit their actual reduced
travel rather than the requested speed; explicit stops clear travel velocity.
This travel component also applies to unmapped fallback rigs, and is separate
from the 20 m/s cap on clip motion. No animated angular velocity is
transferred; severed bodies still receive their separate random spin.
The neck's spherical joint has a zero-speed rotational motor with a 4 N·m
torque limit, providing strong friction against relative head/torso rotation.
Both shoulders use the same 4 N·m rotational brake; elbow hinges use a 2 N·m
zero-speed motor to suppress continued arm flailing. These brakes have no target
pose or restoring spring. Severing a joint removes its brake along with the
connection, preserving detached-part launch and spin; joints still connecting
parts of a detached arm retain their friction.
Capsule ends are inset to avoid starting below ground, and activation adds no
automatic angular launch. Each ragdoll uses a 6 m × 6 m × 0.1 m local ground
box rather than the full terrain mesh. This floor supports only its owning
ragdoll, including severed parts; neighboring corpses cannot land on it.
Collisions between different ragdolls remain enabled. Ground placement and
existing town/tree collision remain independent from animated rendering.

For matching-camera animation captures, use `--animation-time S` to freeze the
idle cycles at a chosen elapsed time:

```bash
./build/godollo_viewer output/godollo.glb --fps --hidden --frames 3 \
  --animation-time 0 --screenshot build/zombies-idle-0.ppm
./build/godollo_viewer output/godollo.glb --fps --hidden --frames 3 \
  --animation-time 0.5 --screenshot build/zombies-idle-1.ppm
```

## Camera and coordinates

| Input | Action |
| --- | --- |
| W / S | Move forward / backward |
| A / D | Strafe left / right |
| Q / E | Move down / up along world Y in free-fly mode |
| Mouse | Look around while captured; held boxes move sideways/up/down; held doors move sideways or push/pull |
| Left mouse button | Combat: shoot; Interaction: left-hand box grab within 2 m or door grab within 1.5 m |
| Right mouse button | Combat with a scoped weapon: cycle normal, 10°, and 1° fields of view; Interaction: right-hand box grab within 2 m |
| 1 / 2 / 3 / 4 / 5 | Equip pistol / submachine gun / shotgun / sniper / HE grenade |
| R | Reload equipped weapon from its matching reserve ammo |
| G | Discard the equipped weapon with a fast forward throw and vertical-axis spin |
| F | Pick up the discarded weapon under the camera ray within 2 m |
| Shift | Free-fly: move four times faster; standing: run at 6 m/s |
| Mouse wheel | Adjust free-fly movement speed |
| F1 | Switch to free-fly mode |
| F2 | Switch to walking mode at the camera position and fall onto the surface below; recover to safe ground if needed |
| Space | Jump while grounded in walking mode; get up first when prone |
| Ctrl (hold) | Crouch on the ground; tuck legs in the air without lowering the camera |
| Physical key left of X: Z (QWERTY), Y (QWERTZ) | Toggle prone in walking mode; an airborne press waits for landing |
| F3 | Toggle collision visualization |
| Tab | Switch between Combat (default) and Interaction controls |
| Left Alt | Release / capture the mouse |
| L | Toggle the flashlight on/off |
| Home | Frame the whole scene again in free-fly mode |
| Escape | Close the viewer |

The mouse is captured when the window opens. Movement uses elapsed frame time
and normalizes diagonal input. Mouse pitch is
limited to avoid flipping the camera. The camera initially frames the imported
scene's transformed bounds, including scenes away from the origin, and chooses
a movement speed based on scene size.

In Interaction mode, aim the center crosshair at a door within 1.5 m and hold
the left mouse button. Walls and solid trunks block the grab. The selected
point stays attached to the door as it swings. Dragging right/left moves a
horizontal grip target sideways; dragging up pushes it forward along the
camera's horizontal heading, and dragging down pulls it back. Moving with WASD
also moves the grip target and tugs the door. Mouse look remains active while
grabbing, so mouse motion rotates the camera and moves the grip together.
A click that misses all reachable objects keeps normal mouse look. Interaction
clicks never shoot. Changing control/camera mode, releasing the cursor, or
losing window focus ends the grab.

The first successful grab of any door in a building also populates its interior
with untextured brown boxes. Each building gets a random target of 12–36 props:
75% small crates (25–65 cm per side), 18% medium boxes, and 7% tall cabinet-sized
boxes (1.65–2.15 m high). Placement may yield fewer in cramped buildings. Boxes
stay inside the roof footprint, avoid courtyards and walls, clear all door hinge
sweeps and approaches, and rest on the published terrain. Grabbing another door
in the same building preserves the existing boxes; populations last for the
current viewer session.

The boxes are dynamic Box3D bodies. Walk into a side to push it, or jump onto a
crate to stand on its top. Boxes can rotate, tumble and stack, and collide with
the surrounding terrain, walls and live doors. The player keeps its existing
capsule controller: it reads oriented box contacts and support surfaces, then
applies bounded, time-scaled impulses during walking. Ordinary clearance,
support and sight queries do not move boxes.

In Interaction mode, aim the center crosshair at a box within 2 m and hold
either mouse button: left and right buttons control independent hands. The left
button selects the nearer door or box; walls, trunks, doors and other boxes
block grabs behind them. Mouse right/left moves the picked point sideways,
up/down moves it vertically, and WASD moves the grip with the player. Camera
look remains active while either hand holds anything. If both hands hold
different objects, mouse movement moves both and rotates the camera once.
Both hands can also share an object; releasing one leaves the other attached.

Grips apply bounded forces to the live body instead of teleporting it. Each hand
can exert 180 N vertically and 240 N horizontally, so one hand can suspend up
to about 18 kg and two hands about 36 kg. Heavier objects respond more slowly
and can be dragged or tipped on the floor. Mouse offsets are bounded, and a
large camera teleport or an object too far from its target ends the grip.
Existing collisions remain active while holding objects.

Release a button to drop that hand's object. Swing the mouse and release within
100 ms to add a throw impulse in the recent motion's direction. Upward swings
also add forward speed equal to 60% of their upward component; diagonal swings
therefore interpolate that boost. Perfect sideways and downward swings add no
forward momentum. Each releasing hand adds at most 60 N·s, giving heavy objects
less speed; an object too heavy for that hand to lift receives no upward boost.
Existing body momentum is preserved. Changing mode, releasing the cursor or
losing focus drops both hands without adding a throw impulse.

The viewer follows glTF's right-handed coordinate system: Y is up, and the
camera looks toward negative Z before orientation is applied. Distances are
interpreted as meters. The vertical field of view starts at 60°. Each
right-click in Combat mode with a scoped weapon selects 10°, then 1°, then
returns to 60° in either camera mode.
Normal view has a small center crosshair; both zoom modes show scope markings
and a circular tunnel-vision vignette whose opening spans nearly the full screen
height. Mouse sensitivity scales with perspective
magnification at both zoom levels: turning is approximately 6.6 times slower at
10° and 66 times slower at 1°, keeping mouse movement consistent on screen.
Normal sensitivity returns when zooming out. Near clipping is 0.1 m in normal
view, 1 m at 10°, and 10 m at 1°. The distance-view near planes preserve depth
precision for closely spaced head surfaces through the scope; geometry closer
than these distances is clipped until zooming out. Far clipping is at least
10,000 m, expanded when framing larger scenes. The
generator's existing root transform maps its east/north/up geometry
to glTF X=east, Y=up, Z=south; no scene-specific rotation is needed in the viewer.
Framebuffer resize updates the viewport and projection aspect ratio.

## Prototype weapons and inventory

Keys **1–4** equip the pistol, submachine gun, shotgun, and sniper. The pistol,
sniper, and shotgun fire once per click; the SMG repeats while left mouse is
held. Every shot respects its configured cooldown and consumes magazine ammo,
including misses and shots in scenes without zombies. Empty weapons stop firing.
The shotgun casts eight rays (one centered and seven around a 4° cone) while
consuming one shell. Firearms use the existing zombie hit/ragdoll behavior.
The sniper alone enables the existing right-click scope cycle. Equipping a
weapon resets zoom. Combat normal view displays [low-poly firearm models](../assets/weapons/README.md):
a 1911 pistol, UMP SMG, a scoped and extended M14 sniper, and a stock pump shotgun.
The models are hidden in scope and Interaction mode. Each firearm kicks upward
and rearward after a successful shot and recovers to its ready pose: a sharp
pistol flip, small fast SMG pulses, a heavy shotgun kick, and slower sniper
recovery. Automatic fire adds a bounded amount of residual recoil. The shotgun's
separate fore-end pulls back and returns between 0.12 and 0.66 seconds after
each shell. Empty clicks and grenade throws do not trigger firearm motion.
Models keep their existing vertices and one draw per held weapon; part motion
uses a pump mask and a translation in the vertex shader. Reload remains instant.

Scoped sniper views have pronounced breathing sway and a short upward firing kick,
scaled with zoom for comparable screen motion at both magnifications. Breathing
reaches roughly 20 pixels from center at 720p.
The reticle stays centered:
the rendered camera and shot direction share the same temporary scope offset.
Hip-fire aim stays unchanged. Motion uses elapsed seconds rather than frame
integration and never accumulates into the mouse/controller camera. Single
player uses the simulation clock. Multiplayer recoil starts on confirmed shots;
sway is computed locally and the resulting aim uses the existing command angles,
without extra animation messages. The muzzle light follows the animated barrel.
Presentation profiles are in `src/weapon_motion.cpp`; there are no new item fields.
Eye-relative positioning uses the configured standing body dimensions: a
two-handed pistol hold and shoulder-seated stocks, with estimated shoulder
width and arm reach. Crouching and prone preserve these upper-body proportions.
The window title and console report the equipped weapon, magazine, and reserve.

**5** equips an HE grenade; **left click** throws one in Combat mode. Holding
the button throws only once. The starting inventory supplies six grenades.
After a throw, the held model disappears until the configured cooldown ends;
an empty inventory keeps the hand empty. Switching weapons or reloading does
not bypass the cooldown. Multiplayer follows the server-confirmed readiness.
The original mesh is a medium grey octagonal column with red stripes near both
ends, shared between the held and thrown model. Grenades use the same Box3D
world, streamed town colliders, and live doors/boxes/ragdolls as interior props.
Tree rebounds instead query the existing trunk BVH after physics, using a
conservative 9 cm grenade sphere and mathematical velocity reflection. No tree
bodies are added to Box3D. Trunk sides preserve vertical and tangent velocity;
rounded ends use a 3D normal. These are overlap checks, so fast throws can pass
through thin trunks, especially at low frame rates. Tree hits preserve the
grenade's spin and do not apply tangent friction. Foliage remains rendering geometry.
Their 450 g bodies have octagonal convex colliders and continuous collision
detection. Throws inherit walking velocity and add a small upward arc.

The 3-second fuse starts on the throw. Detonation removes the body, kills exposed
zombies within 8 m into ragdolls, pushes exposed dynamic objects outward, emits
AI noise, and plays a synthesized explosion sound in single player. Solid scene
geometry, closed doors, and props can shield targets. The existing prototype has
no player health/damage system. A warm explosion point light uses the same pulse
code as gun muzzles, with a 12 m range and 180 ms quadratic fade. Up to eight
explosion lights can coexist with the gun muzzle light, including with **F** off.
Nearby old-forest shrubs bend outward and settle with a short rebound within
0.9 seconds, using their existing cards and matching shadow poses. The visual
response fades over 12 m and creates no particles or extra mesh vertices.
Multiplayer throws, collisions, fuses and blasts run on the server; clients
interpolate the replicated bodies and fade explosion events by their age.

[he_grenade.toml](../items/he_grenade.toml) uses `kind = "he_grenade"` and adds
`fuse_seconds` (0–30, exclusive of zero), `blast_radius` (0–50 m), and
`throw_speed` (0–30 m/s). All three must be finite and positive. Grenade inventory
counts are whole grenades; `muzzle_position` is unused for throws.

The optional [grenade tree CPU benchmark](../tests/benchmark_grenade_trees.cpp)
runs without a display, using 40,401 synthetic trunks (4 m spacing, 25 cm radius,
12 m height), flat ground, 14 m/s throws and a 60 Hz simulation. It measures scene
preparation, shared physics and grenade updates; rendering and initial terrain/BVH
construction are excluded. Full throws visit new regions in twelve measured
waves after two warmup waves. Repeated impacts reset position, orientation and
velocity outside the timer, then measure two simulation steps per approach.
One- and sixteen-grenade batches have matching open-ground controls, followed
by idle measurements after the full throws. Build and run explicitly:

```bash
cmake --build build --target benchmark_grenade_trees -j4
./build/benchmark_grenade_trees
```

Comparison on 2026-10-19 used the previous Box3D-trunk implementation at
`1fecfd4`, saved before replacement, and this overlap implementation. GCC 14.2,
Release (`-O3 -DNDEBUG`), Ryzen 5 7535HS, one CPU core; seven paired runs with
alternating version order. Values below are medians of each run's mean elapsed
time per simulation tick, including all three measured stages:

| Forest workload | Box3D trunks | Sphere overlaps | Time reduction |
| --- | ---: | ---: | ---: |
| Repeated impacts, 1 grenade | 74.506 µs | 70.455 µs | 5.4% |
| Repeated impacts, 16 grenades | 175.971 µs | 119.605 µs | 32.0% |
| Full throws, 1 grenade | 64.887 µs | 51.906 µs | 20.0% |
| Full throws, 16 grenades | 127.820 µs | 96.027 µs | 24.9% |
| Idle after sixteen-grenade waves | 15.846 µs | 15.874 µs | Unchanged |

Open-ground controls differed by less than 0.5% in their median means. Full-throw
results include changed bounce trajectories, ground contacts and sleep; repeated
impacts give the closer comparison of collision handling. The old version retained
42/672 tree bodies after the one-/sixteen-grenade tours; the new version retains
zero. Idle timings show that those static bodies were not a significant cost in
this fixture. These are small absolute CPU savings, not an end-to-end forest FPS
measurement or a promise of a similar percentage gain in rendered frame rate.

Each [weapon TOML](../items/) defines `name`, `ammo_type`,
`shot_interval_seconds`, `automatic`, `has_scope`, `pellets`, `spread_degrees`
(cone half-angle), `magazine_capacity`, `ammo_per_shot`, and `muzzle_position`.
Firearms also require `caliber = "low"` or `"high"`. The pistol and SMG use low,
the sniper uses high, and the shotgun uses low per pellet (1 body / 2 head damage).
The muzzle is a required three-number array in the prepared model's local meters:
X right, Y up, forward -Z. Its position is the center of the barrel's front cap,
before applying the eye-relative weapon pose and camera-to-world transform.
Counts use rounds
or shells, rather than pellets. `pellets` supports 1–64; `ammo_per_shot` must be
positive and fit the magazine; cooldown must be positive and spread in [0°, 90°).
The viewer uses [toml++](https://github.com/marzer/tomlplusplus), pinned to 3.4.0
through CMake's dependency cache. Invalid or missing required values fail startup
with a configuration error.

Each successful firearm shot briefly activates a warm point light at that muzzle. It
illuminates nearby scene and weapon surfaces in daylight and at night, has an
8 m range with distance falloff, and fades quadratically to zero over 75 ms.
Automatic fire restarts the pulse per shot; shotguns pulse once per shell and
empty clicks produce no flash. Scope mode hides the mesh but keeps the muzzle
light. The multiplayer client pulses on server-confirmed local shots. This
short-lived point light does not render additional shadow maps.

[inventory.toml](../inventory.toml) chooses the initially `equipped` weapon.
Its `[weapons]` table lists owned weapon IDs and their starting loaded rounds;
omit a weapon to remove ownership. `[ammo]` holds additional reserves for the
five separate `ammo_type` values, including zero reserves. **R** instantly
transfers matching reserve rounds into the equipped magazine, allowing partial
reloads when the reserve is low. Firing, switching, and reloading preserve each
weapon's own cooldown. Reloading and equipping require a fresh trigger press;
cursor release, focus loss, and camera/control-mode changes also clear held fire.
At most one shot fires per rendered frame; stalls do not produce catch-up bursts.
Semi-auto clicks during cooldown are discarded. Configuration and inventory are
read once at startup; gameplay changes live in memory and restart from the file.

### Discarding and picking up weapons

In the standalone viewer, **G** discards the equipped weapon with a 12.5 m/s
forward launch, a small upward velocity, and a 14 rad/s spin about world Y.
Actual player travel adds to the launch. The dropped item renders its weapon
model and uses a box collider fitted to the model bounds, with continuous
collision detection and the same streamed terrain/building physics as crates.
After the first ground contact, strong linear and angular damping brakes the
weapon through sliding and bounces, targeting roughly two metres to settle.
Doors and other physics props can also stop it. A blocked spawn leaves the
weapon in the inventory. Discarding an HE grenade creates an unprimed pickup.

**F** casts the camera's forward ray against dropped weapon boxes within 2 m.
Scenery blocks pickup. A successful pickup destroys the physics object and
equips its weapon, preserving loaded rounds and its existing shot cooldown.
Reserve ammunition stays with the player throughout. Already-owned weapon types
are left in the world. Dropping selects the next owned weapon; dropping the last
one leaves the player unarmed, without firing, reloading, or a held model.
These actions work in either control mode while the mouse is captured.
The flashlight toggle is **L**. Multiplayer drop/pickup input and replication
are not included in this standalone feature.

## First-person walking

Launch the generated town, then press **F2**, or start directly in walking mode:

```bash
./build/godollo_viewer output/godollo.glb --fps
./build/godollo_viewer output/godollo.glb --spawn 0 0
./build/godollo_viewer output/godollo.glb --fps --collision-debug
```

`--spawn X Z` requests a location in the loaded scene's local meter coordinates;
Y is found from the terrain. `--fps` and `--spawn` search for clear, walkable
terrain under or near the requested position. **F2** preserves the free-fly eye
position when the player capsule fits and a scene surface exists beneath it,
then gravity drops the player onto that surface, including building roofs.
Clear interiors can also retain the camera position. If the capsule overlaps a
wall, roof, trunk, or live door, or there is no surface below it, F2 uses the
safe ground search instead. The viewer logs mode changes. **F1** releases the
camera from the controller at its current eye position; gravity applies only
in walking mode.

The player is a vertical capsule, 1.80 m tall and 0.30 m in radius. The camera
sits 1.70 m above its feet. W/S/A/D movement uses yaw, independently of look
pitch, so looking up or down cannot make the player fly. Diagonal input is
normalized. Walking speed is 3 m/s; Shift increases it to 6 m/s. Gravity is
9.81 m/s² and a grounded Space press launches at 3.8 m/s, producing an apex
roughly 0.74 m above standing height on flat ground. Holding Space does not
repeat jumps, and presses in the air do not add another jump.

Holding either Ctrl key crouches to a 1.10 m capsule with a 1.00 m eye height
and 1.5 m/s movement speed. Toggling prone uses a 0.60 m capsule, a 0.50 m eye
height, and 0.75 m/s movement. Prone persists after releasing its key and takes
priority over Ctrl. Shift does not accelerate crouching or crawling. Space
requests getting up from prone; another press can jump. The prone key uses the
physical US Z position (left of X), independent of the printed letter; the
existing console help uses GLFW's layout-specific key name. Rebinding is not
implemented.

Grounded stance changes keep the feet planted and lower or raise the camera.
Airborne Ctrl instead lifts the collision feet by 0.70 m, preserving the head's
ballistic path, vertical velocity, and takeoff movement speed. It can clear a
taller obstacle without increasing jump height. Releasing Ctrl extends the legs
only if the larger capsule fits; otherwise it retains the tuck. Grounded
standing is also blocked by low ceilings and resumes when there is space.
Prone is a short upright collision capsule rather than a full horizontal body;
there is no rendered player body or leg animation. Collision, debug geometry,
and zombie head/body/leg perception targets all use the current stance dimensions.

Collision data is built from the existing loaded GLB, with scene-node transforms
applied in the same Y-up coordinates used for rendering. The loader preserves
node/mesh names, identifying the generator's terrain, buildings, and roads.
A static triangle BVH limits collision queries to nearby geometry. Exact
capsule-to-triangle contacts follow building footprints, including concave
outlines, rather than replacing buildings with broad rectangular obstacles.
Named buildings also have an interior query for safe spawning. Roads and
other triangle geometry can provide support; a separate collision file is not
required. New maps render an alternative wall mesh with real door/window
openings and flush brown doors and blue glass with 60% transparency. Within
120 m by default, the original opaque walls are replaced by the detailed walls
and panels together. Both scope views extend this range and retain frustum
culling. Walking collision cuts the doorways out of the original wall triangles
and queries the door panels at their current Box3D poses. Windows remain solid.
Building shadows continue to use the original closed shell.
There are no modeled rooms. See [building generation](../README.md) for
configuration and regeneration.

Generated brown door panels have runtime Box3D bodies and vertical revolute
joints, sharing the simulation with zombie ragdolls. Their limits allow a
**0–110° outward swing**, selected from each facade's outward normal. A thin
4 cm box supplies contact geometry for the flat rendered panel; the collider
starts 10 cm above the highest lower boundary to clear the 8 cm foundation
and leave 2 cm above ground.
A static anchor holds the hinge. Panels weigh 25 kg and have angular damping;
there is no latch or automatic closer. Interaction controls apply forces to
the point selected when a grab begins.

`prepareDoorPanels(model)` separates batched door meshes at load time without
changing the GLB or requiring regeneration. It preserves the original node
transforms, materials and building LoD groups. An immutable visibility BVH
includes the panel's full horizontal sweep, so normal and scoped frustum
culling remain conservative when the panel moves. Far LoD walls still
approximate the building as closed.

`DoorLayer` owns the bodies in a shared `PhysicsWorld`. `activateNear(position,
radius)` uses a grid to instantiate local hinges (32 m by default); the app
calls it around the camera and ragdolls. Other future physics actors must call
it around their positions before stepping, with a radius covering their next
movement. Activated doors retain their pose and can sleep as the camera leaves;
they are not recreated or reset. `applyForce(door, force, worldPoint)` and
`applyImpulse(door, impulse, worldPoint)` activate a chosen door directly.
Vectors use world coordinates, meters, newtons and newton-seconds. Reapply a
sustained force each physics step. Physics objects created in `PhysicsWorld::id()`
can push doors through ordinary contacts, with no door-specific hit handler.

`DoorLayer::raycast` uses the spatial grid and current oriented panel boxes;
picking is read-only. It returns a stable door index and a body-local hit point.
`pointState` follows that point's world position and velocity as the hinge
rotates. `DoorInteraction` owns the control mode and grab state independently
of GLFW. Its horizontal spring has stiffness 220 N/m, damping 35 N*s/m, and a
180 N force limit. Mouse travel moves the target by 4 mm/pixel, clamped to
1.5 m on each axis. The target also follows player displacement. Each update
integrates the force as a dt-scaled impulse, so input survives render frames
with zero or multiple fixed physics steps. The app updates this after walking;
the next shared physics step advances the panel.

`CollisionWorld(model, trunks, &doors)` borrows the door layer, which must outlive
the collision world. At construction it clips just the door rectangles from
matching v2 low-detail walls; the generated map stays unchanged. This avoids
importing the much larger window-cut facade meshes. Jambs and lintels remain
physical surfaces. The existing terrain continues underneath the empty building
shell and supplies indoor ground support; spawning still avoids interiors.
Legacy overlay maps keep their visible solid walls and need regeneration to
obtain actual doorways.

Walking clearance uses Box3D's capsule-versus-box contact manifold and the
panel's live body position, rotation and thickness. A partly open panel blocks
only the space it occupies; a gap is usable when the capsule actually fits.
There is no open/closed flag or opening-angle threshold. Collision queries are
read-only and do not push or activate doors. Inactive panels use the same closed
box until physics activation. Spatial queries use conservative hinge-sweep
bounds, independently of camera visibility or scope LoD. Sight queries and the
collision overlay follow moving door geometry too.

Advance `PhysicsWorld::step(dt)` once per app frame, then call
`ZombieLayer::synchronizePhysics()` before updating walking, and
`Renderer::updateDoors(doors)` before drawing. Walking and rendering thus use
the same physics poses. `ZombieLayer::attachPhysics(world)` must run before the first ragdoll;
its standalone `update(dt)` continues to advance its own simulation for tests
and independent callers. Ragdoll and door owners destroy their bodies before
releasing the shared world. The rigid-body world contains doors, dynamic interior
boxes and the existing ragdoll ground boxes. Ragdoll fallback floors do not
collide with interior props. `PhysicsScene` lazily imports the BVH-selected town
triangles around props into Box3D, retaining actual terrain heights and clipped
doorways. It uploads each triangle once, with both collision sides, and expands
coverage as props travel. These imported mesh contacts apply to props;
doors retain their authored hinge limits without catching their finite collider
thickness on a flush wall jamb. Props still collide with live door bodies.
Mesh data stays alive through shape destruction.
Call `PhysicsScene::prepare(boxes)` before the shared physics step and
`Renderer::updateBoxes(boxes)` before drawing. Rendering shares one cube mesh
and uses an instanced draw per pass, with a BVH over current transformed bounds.
Sleeping bodies retain their cached render data until motion or spawning changes
the layer revision. Walking continues to query `CollisionWorld`, which borrows
the box layer through `setBoxes()`.

The controller uses fixed 120 Hz simulation and further divides large capsule
displacements. Iterative contact corrections stop motion into walls while
preserving movement along them. Ground normals guide walking on slopes up to
45°; steeper uphill contacts block climbing. A short ground snap keeps downhill
walking in contact with terrain. Leaving a larger drop enables falling, and
landing cancels downward velocity. The shared capsule step check climbs sharp
edges up to `FPSConfig::stepHeight` (0.30 m by default) when there is walkable
support and body clearance. It is enabled only during grounded horizontal
movement, without granting an airborne step or a jump. A 0.30 m support snap
keeps the player on descending terrain and small drops.

The step limit is measured above the supporting slope at the ledge, so a
30 cm road lip remains traversable when the feet are still downhill from it.
The player attempts this step before edge collision correction can lift the
capsule off the ground. Both player and zombie checks retain ceiling, wall,
whole-body clearance and crowd constraints.

Roads default to a 0.02 m base offset above terrain. Terrain-ridge clearance
corrections may raise individual sections further; a configured offset is not
an exact bound on every rendered road-to-ground gap. Older maps generated with
a 0.25 m offset can have edges over 0.30 m. To lower only their roads without
regenerating the town, set `roads.offset_m` in `config.json`, then run:

```bash
.venv/bin/python tools/update_road_offset.py --from-offset .25 \
  --companion output/trees.instances.json \
  --companion output/poles.instances.json \
  --companion output/guardrails.glb --companion forest_patches.json
```

Supply the actual source offset and only companions present for that map.
The updater translates road leaf nodes in world Y, preserving all mesh buffers,
road footprints, terrain and buildings. It validates the candidate GLB and
unchanged terrain before publication, saves originals under
`output/road-offset-backups/`, and refreshes only companion fingerprints matching
the original map. Placements and companion geometry stay intact. A stale
companion fingerprint is rejected. New generations record the road offset so
accidentally repeating a patch with the old source offset is rejected too.

Important dimensions, speeds, gravity, jump strength, slope limit, contact skin,
ground snap, and simulation timestep live together in `FPSConfig` in
`src/fps_controller.hpp`. Change these defaults and rebuild to tune the controller.
Configured heights and eye height describe standing; `bodyHeight()` and
`eyeHeight()` return current dimensions. Reduced heights are clamped to fit the
capsule diameter and the configured eye inset for custom controller profiles.
The default contact skin is 0.002 m. On slopes the rounded capsule's bottom sits
slightly above the terrain height at its center, so eye height measured vertically
from that point can be slightly greater than 1.70 m.

**F3** overlays the player shape (green when grounded, red while airborne), nearby
collision geometry (amber), and ground contact normal (cyan). The window title
also reports whether the player is grounded.
Lines remain visible through rendered geometry. Switching back to free-fly with
the overlay enabled is useful for viewing the player shape from outside.
`shaders/debug.vert` and `shaders/debug.frag` provide the simple line shader.

Collision is static: animated or moving geometry does not update the BVH.
For arbitrary unnamed glTF geometry, closed surfaces within one draw can be
recognized as solid interiors for spawning. A solid split across unrelated
draws may only receive surface collision; named generated buildings are grouped
across their wall and roof draws.
The controller is a lightweight walking controller with no swimming,
moving platforms, or navigation system. Terrain gaps in the source geometry
remain gaps, and walking beyond the generated terrain causes falling. The
interactive viewer clamps frame time to 0.1 seconds after stalls; the standalone
controller also caps accumulated catch-up time at one second.

## Geometry, materials, and shaders

The loader reads binary `.glb` and JSON `.gltf` files, including their buffer and
image resources. It supports multiple meshes and primitives, scene-node
transforms, indexed and non-indexed triangles, triangle strips/fans, vertex
positions, normals, `TEXCOORD_0`, vertex colors, and sparse/interleaved accessors.
Missing normals are generated from the triangle geometry. Materials use base
color factors and PNG/JPEG base color textures where available; geometry without
a material has a shaded fallback color. Texture wrap/filter settings,
double-sided materials, alpha masks/blending, `KHR_materials_unlit`, and
`KHR_mesh_quantization` are supported. Missing or unsupported images produce a
warning and a color fallback.

`shaders/basic.vert` applies the model, view, and projection matrices.
`shaders/basic.frag` provides directional Lambert lighting, sky/ground ambient
illumination, and nearby sun shadows. These GLSL 330 files are read at startup,
so edit them and restart the viewer to experiment.
Use `--shader-dir /path/to/shaders` to load an alternate shader pair.
Shader errors print the source filename and compiler output. Model errors
include the input path. Debug builds print OpenGL debug messages when the
driver exposes the debug extension.

The main scene loader displays static geometry; idle skeletal animation is
provided by the separate zombie layer described above. The viewer does not
provide morph targets or full PBR lighting.
Normal/metallic/roughness maps and texture coordinate sets beyond
`TEXCOORD_0` are not rendered. Transparent primitives are sorted as whole
objects, so intersecting transparent surfaces can show sorting artifacts.
Non-triangle primitives are skipped with a warning. Unsupported required glTF
extensions (including Draco/meshopt compression and KTX2 textures) are reported
as errors. Large
scenes are loaded into memory as a whole; there is no streaming or level of
detail system. Very large absolute coordinates remain subject to floating
point precision limits.

## Lighting and shadows

Generated chain-link and railing fence masks retain their glTF alpha cutoff
up close. As the texture footprint grows, the viewer smoothly lowers the cutoff
to 0.2 for wire or 0.3 for railing, keeping thin features visible through mip
filtering. Denser patterns raise the floor above their averaged opacity so
relaxed filtering does not turn distant fences into solid sheets. Fence textures
also use up to 4× anisotropic filtering when available for views along the fence. The tuning
applies in both color and shadow passes, to existing maps without regeneration;
other material masks keep their original cutoff.

Run `./build/godollo_viewer output/godollo.glb --no-fences` to disable generated
fence panels and posts, their shadows, and their walking/perception collisions.
This runtime option works with existing maps and can be combined with other
comparison flags. Fences are enabled by default.

Lighting is tuned for FPS walking: one warm directional sun plus cool sky and
muted ground ambient light. Ambient illumination remains in shadow. Linear RGB
lighting is applied before the existing sRGB output encoding. The sun direction,
color/intensity, and ambient colors are set by `SunLighting` in
`src/sun_lighting.hpp` and can be changed through `Renderer::setLighting` for
fixed lighting. The application's day/night cycle supplies these settings through
`Renderer::setDayNightLighting`.

### Day/night cycle

The viewer starts at noon and advances through a full day in 1200 simulation
seconds (20 minutes). The window title shows the game clock. Choose the starting
hour with `--time-of-day H` in `[0, 24)` and the duration of a full day with
`--day-length S` (at least one second). Use zero to freeze the selected hour:

```bash
./build/godollo_viewer output/godollo.glb --fps --time-of-day 17 --day-length 600
./build/godollo_viewer output/godollo.glb --fps --time-of-day 17.7 --day-length 0
./build/godollo_viewer output/godollo.glb --fps --time-of-day 0 --day-length 0
```

The stylized sun rises in the east at 06:00, passes to the south at noon, and
sets in the west at 18:00. It is an artistic cycle, not a date/location-based
solar calculation. Sun color, sky and ground ambient light, unlit tree colors,
sky/fog color, camera flashlight strength, and rain lighting blend smoothly.
The cycle uses the same clamped simulation time as physics and remains independent
of `--animation-time`; freeze the day separately for repeatable captures.

Shadows follow the moving sun using the existing local depth map and caster
pass each frame. Direct sunlight fades smoothly from full strength at 12 degrees
to zero at 4 degrees above the horizon; twilight ambient light remains. This
reduces visible artifacts from long shadows without expanding the fixed caster
volume. Very tall or distant casters can still exceed that volume. At zero
sunlight the pass is skipped and shadow sampling is disabled, then both resume
at dawn. There are no additional render passes or shadow maps for the cycle.
Clock and lighting calculations are in `src/day_night.hpp/.cpp`.

### Local sun shadows

One 1024 × 1024 depth map follows the camera. Use `--sun-shadow-resolution 2048`
to compare the previous resolution; the option accepts 256–8192 pixels per side,
subject to the GPU texture-size limit. This changes sunlight shadows for all
casters. Flashlight shadows keep their separate 1024 × 1024 target. Shadows fade smoothly between
90 and 120 meters from the eyes; ordinary shading continues beyond that range.
Coverage is independent of view direction and zoom. The light-space origin is
snapped to texels to reduce swimming during movement; four filtered depth
comparisons soften edges, with slope-aware bias to reduce self-shadow artifacts.
The texel-snapping grid and depth-comparison footprint follow the selected map
resolution. Halving each dimension reduces map pixels/storage to one quarter;
caster geometry and world-space coverage stay the same. Boundary caster
selection can shift slightly because the light-space snapping grid changes.
This is a local shadow map, with no cascades for distant overview cameras.

Opaque and alpha-masked town geometry, zombies, and tree cards cast shadows.
The town BVH queries the light volume separately from camera visibility, including upstream
casters outside the view. Zombie casters are filtered by conservative animated
or live-skin bounds and remain instanced by asset/primitive. Both passes share
the same baked animation and live walking/ragdoll skinning. Transparent blended
surfaces do not cast shadows. Tree casters use the existing tree spatial index
to query the light volume independently of camera visibility, then batch
instances by asset and card. Their opaque texels write shadow depth and their
transparent texels are discarded. Instance matrices and asset-node transforms
compose identically in both passes, including reflection and shear. The color
pass uploads its own instance population and retains its visibility statistics.
Tree impostors stay unlit and do not receive sun shadow maps. Their separate
static canopy-cover shading uses no shadow map and leaves flashlight illumination
unchanged. Other unlit materials bypass sun/ambient shading. All visible
materials receive haze and ground fog.
The crossed photo cards produce approximate canopy shadows, rather than the
silhouette of a full 3D tree. No extra shadow map or separate pass is added.

Compare the same FPS view with shadows disabled:

```bash
./build/godollo_viewer output/godollo.glb --fps --no-shadows
./build/godollo_viewer output/godollo.glb --fps --no-tree-shadows
```

The existing asynchronous GPU timing includes the shadow pass and color pass;
presentation and VSync remain outside it. `--no-town-culling` changes color-pass
culling only; shadow casters still use the local light volume. Large individual
terrain/road meshes remain whole draw calls even when only part overlaps it.

## Night mode and flashlight

### Fixed night mode

Enable fixed night mode explicitly at launch:

```bash
./build/godollo_viewer output/godollo.glb --fps --night
```

Without `--night`, the viewer follows the day/night cycle. `--night` freezes
the existing night lighting and cannot be combined with `--time-of-day` or
`--day-length`. Fixed night mode disables sunlight and its shadow pass, uses
weak blue ambient light, and changes the sky and fog to a
dark matching color. Fog density, height, and start distances keep their current
settings; `--no-haze` and `--no-ground-fog` still work.

A warm flashlight is held 25 cm right, 15 cm below, and 10 cm ahead of the
camera eyes. It follows mouse look and walking and aims at the reticle's
ten-meter target. The small offset makes cast shadows visible beside objects.
Its bright core extends 12 degrees from the viewing axis, fading to zero at
22 degrees. Brightness decreases with distance and fades out completely at
40 meters. It illuminates terrain, buildings, zombies, and visible tree texels.
Tree cards retain their alpha masks and bypass normal-based shading, but receive
night dimming and the flashlight's cone/distance attenuation.

Press **L** to toggle the flashlight in either control mode. It starts enabled;
turning it off removes the beam, its optional shadows, and its illumination of
rain, while preserving the night palette and weapon muzzle flashes. The setting
persists through the day/night cycle; daylight still fades the beam out normally.
**Home** frames the whole scene (previously **F**).

Flashlight shadows are disabled by default. Enable them with
`--flashlight-shadows`, for example:

```bash
./build/godollo_viewer output/godollo.glb --fps --night --flashlight-shadows
```

They use one 1024×1024 perspective depth map, with a 5 cm near
plane and 25 m coverage. Shadows fade out between 20 and 25 m; illumination
still reaches 40 m. The map is allocated lazily and rendered only while night
lighting contributes, including twilight. Its cone is independent of scope
zoom. Town, tree, forest, and zombie casters are culled against this light volume
independently of the camera, so offscreen objects can still cast onto visible
surfaces. Nearby building details and moving doors cast their actual silhouettes,
rather than the original closed building shell. Alpha-mask holes remain open;
blended glass does not cast. Tree/photo-card shadows are approximate.

The color pass applies four filtered depth comparisons only to surfaces inside
the beam and shadow range. This shadows the flashlight contribution, including
on unlit tree cards, while preserving ambient and sunlight. Zombies use the
original mesh inside 12 m and the first mesh LoD beyond that, preserving the
camera's animation pose where visible. Camera, sun, and flashlight share one
persistent ragdoll palette cache; settled palettes do not upload again.

Use `--no-flashlight-shadows` for a direct cost comparison. `--no-shadows`
disables both lights' shadows; `--no-tree-shadows` and `--no-zombie-shadows`
disable their respective casters for both lights. The asynchronous GPU timer
includes this extra pass. Its cost depends on nearby geometry and caster density;
large terrain/road primitives remain whole draw calls when intersecting the cone.
Rain streaks follow the same offset light but do not sample the shadow map.
There is no visible volumetric beam or light scattering through fog.
Night colors, cone angles, ranges, and shadow resolution are configured in
`src/night_lighting.hpp`; the held-light pose is in `src/flashlight.cpp`.

## Rain

Rain is disabled by default. Enable it with `--rain`, optionally together with
night mode:

```bash
./build/godollo_viewer output/godollo.glb --fps --rain
./build/godollo_viewer output/godollo.glb --fps --night --rain
```

One instanced draw renders 3,000 soft translucent streaks, each made of two
triangles. A static seed buffer is uploaded once; the vertex shader makes them
fall at roughly 10 m/s with light wind. They recycle through a 36 × 16 × 36 meter
volume around the camera, staying fixed in world space between recycling events.
The volume edges and closest drops fade out. Streaks face the camera and shorten
when looking along their falling direction.

Rain draws after the scene with depth testing, additive blending, and no depth
writes. Buildings and opaque tree texels hide rain behind them; alpha-mask holes
remain open. Rain does not cast shadows or collide with surfaces. In night mode,
streaks inside the flashlight cone brighten. `--animation-time` freezes rain as
well as character animation for repeatable captures.

There is no shelter detection: drops can appear under roofs and inside buildings.
There are no splashes, wet materials, or puddles in this version. Count is set in
`src/rain_renderer.hpp`; volume, width, length, and velocity are in `shaders/rain.vert`.

## Atmospheric haze

Subtle exponential distance haze is enabled by default. The nearest 30 meters
remain clear; beyond that, distant terrain, buildings, trees, and zombies blend
gradually toward a pale blue-gray sky. Default density is 0.0008 per meter,
giving roughly 31% haze at 500 meters and 54% at 1 kilometer. The background
matches the distant haze color to keep the horizon continuous.

Haze runs in the existing color shader after material lighting and before sRGB
encoding. It preserves alpha masks, blended-material alpha, and depth writes;
the shadow pass bypasses it. There are no extra textures or render passes, and
the reticle and collision overlay remain clear. Haze does not change draw
distance or visibility culling. Distance haze is uniform and independent of
the height-dependent ground fog below; neither uses volumetric lighting or
light shafts.

`src/atmosphere.hpp` defines the linear RGB color, clear foreground distance,
and density; `Renderer::setAtmosphere` can customize them. Compare the same view
with haze disabled (the daylight sky color remains):

```bash
./build/godollo_viewer output/godollo.glb --fps --no-haze
```

## Analytical ground fog

A ground mist layer is also enabled by default. Its base is anchored
once to the player's feet at the first entry into walking mode. The anchor uses
the runtime Y-up meter frame and does not follow later movement, hills, or jumps.
Before entering walking mode, the base is world Y=0, the map's recorded datum.

Density is 0.048 per meter at and below the base, then decreases linearly to zero
over 18 meters of height. Mist starts at the eyes by default; `startDistance`
in `src/atmosphere.hpp` can reserve a clear foreground in meters.
The color shader integrates this profile along the eye-to-surface ray beyond
that clear foreground. It handles horizontal rays without division by a tiny
height difference and includes mist crossed while looking into or out of the
layer, even if the visible surface itself is above it.

The integrated optical depth adds to distance haze, sharing its color and one
final transmission calculation in linear RGB. There is no ray marching, extra
texture, render pass, or lighting/shadow interaction. Terrain, buildings, tree
cards, and animated zombies receive mist while retaining their material alpha
and depth behavior. Opaque geometry naturally limits the ray to its visible
surface. The reticle, debug overlay, and shadow map remain unaffected.

This is one horizontal layer, so it pools in lower terrain and clears over
higher terrain rather than following every hill. It has no wisps, local fog
banks, indoor exclusion, or weather simulation. `GroundFog` in
`src/atmosphere.hpp` defines its base height, layer height, density, and clear
distance; `Renderer::setAtmosphere` can customize them.

Disable just the mist, or both atmospheric effects, for comparisons:

```bash
./build/godollo_viewer output/godollo.glb --fps --no-ground-fog
./build/godollo_viewer output/godollo.glb --fps --no-ground-fog --no-haze
```

## Verification and diagnostics

See [standard benchmarks](BENCHMARKS.md) for tracked source, reproducible commands,
timing reports and the required 100,000-zombie open-area rendering case.

The window title updates about once per second with
`FPS | GPU <average> ms | CPU render <average> ms | CPU sim <average> ms`.
FPS uses the actual frame
interval, including any VSync wait. GPU time uses asynchronous
`GL_TIME_ELAPSED` queries for the scene and enabled collision overlay; CPU
render time measures time spent submitting those draws, including driver
overhead and stalls. Both render timings exclude events, physics updates,
framebuffer resize, the presentation blit, and the buffer swap/VSync wait.
`CPU sim` measures the simulation portion before renderer preparation; it includes
zombie AI, player/camera updates, door activation and physics. Add `--profile` to
print those timings plus zombie behavior and preparation times. Preparation
includes renderer instance/bounds updates, resize and collision-overlay assembly;
it is outside both the simulation and render measurements. A low CPU render time
alone therefore does not establish that the whole frame is inexpensive. Flying
through a large crowd can accumulate active pursuers whose simulation continues
even after camera distance culling hides them or the camera returns to FPS mode.
GPU results can lag and show `pending` or `unavailable` as appropriate. Compare
CPU and GPU timings separately: their work overlaps, so the times cannot be
added. Timing appears automatically in the title without additional console
output or command-line options.

Run the viewer checks:

```bash
ctest --test-dir build --output-on-failure
```

The CPU-only [frustum visibility benchmark](VISIBILITY_BENCHMARK.md) compares
fixed plane order with temporal plane hints across synthetic scenes and a loaded
map. It also covers the horizontal range limit used inside old forests.

`viewer_day_night` checks clock wrapping/freezing, east-to-west sun movement,
day/night endpoints, horizon light fading, and continuity across the full day.
`viewer_sun_lighting` checks local receiver coverage, upstream caster coverage,
and texel stability without a GPU. `viewer_shadow_renderer` checks offscreen
casters, tree alpha-mask holes and mirrored/sheared node composition, separate
tree shadow/color populations and statistics, ambient illumination, unlit
materials, distance fading, animation/live-skin shadow poses, target resizing, and GL state on an
accessible OpenGL display; it skips when no display/context is available.

`viewer_atmosphere_renderer` checks clear foregrounds, increasing haze and sky
convergence across scene/tree/zombie draws, alpha-mask holes, blended alpha,
linear color blending, the disable options, and empty-sky screenshot detection.
Ground fog checks cover layer crossings, horizontal rays, translated world
heights, clear air above the layer, and comparison with independent numerical
ray integration. Night checks cover scene/tree/zombie dimming, dark fog,
flashlight cone edges, camera tracking, range cutoff, and daylight restoration.
`viewer_flashlight` checks the held-light pose, zoom independence, and bounded
cone without a GPU. `viewer_shadow_renderer` also checks flashlight shadows
from opaque/alpha-mask geometry, moving doors, trees, and live ragdolls,
including palette sharing during twilight, fade/cutoff, and restored GL state.
For synthetic flashlight captures, run
`./build/test_shadow_renderer build/flashlight-check`; it writes `.on.ppm` and
`.off.ppm` comparisons.
Rendering tests skip without an accessible OpenGL display/context.
Geometry/palette and shadow regressions disable atmospheric effects to isolate
their expected colors.

`viewer_fence_renderer` compares wire and railing visibility at 20 and 30 metres,
including oblique views, with fog disabled. It checks the magnified mask, gate
gaps, depth writes, and sparse distant coverage, and skips without a display.

`viewer_rain_renderer` checks visible animated streaks, repeatable frozen time,
the enable/disable toggle, night flashlight illumination, opaque depth occlusion,
and restoration of GL program/VAO, depth, culling, and blend state. It requires
an accessible OpenGL display/context and skips when one is unavailable.

The `viewer_zombie_renderer` regression checks mixed idle/walk/run clips, clip
wrap interpolation, camera culling and reentry, frustum-crossing animated bounds,
shared group culling, cell crossings, unchanged-group reuse, and pose/removal updates,
retained and deferred ragdoll palettes, offscreen shadow casters, GPU storage
growth, dirty-range coalescing, slot reuse, and characters with multiple
primitives through the actual shader. It requires
an accessible OpenGL display and reports a skip when none is available. The
remaining checks run without a display.

`viewer_zombie_crowd` checks swept contacts, sliding, opposing movers, separate
floors, inactive blockers, animation stopping, ragdoll release, grid updates,
stable movement priority, and conservative overload handling. Two replays of a
converging 10,000-body crowd check identical positions, bounded neighbor work,
and final torso separation using an independent sweep-and-prune check. Reported
elapsed time is diagnostic, with no machine-dependent timing assertion.

`viewer_zombie_movement` covers thin walls from both sides, sliding/corners,
transformed scene nodes, mapped pole geometry, fence entrances/toggles, low
guardrails, trunks, closed/open/rotated doors, read-only box collision, blocked
behavior and animation, and asset normalization. A 1,000-zombie wall convergence
fixture verifies deterministic replays, bounded crowd checks across substeps,
and separation after combining world and crowd constraints, without a GPU.

`viewer_zombie_motion` records positions, facing, selected clip and interpolated
animation frames in deterministic crowded convergence, wall and moving-door
fixtures. It checks retained tiny backward slides, bounded turning, absence of
rapid clip switching, and continuous pursuit of a moving target. Separate
30/60/144 Hz checks cover held gait poses, sustained blockage, resumed movement
and deliberate reverse turns. To save CSV traces using an actual animation asset:
`./build/test_zombie_motion build/zombie-motion-trace assets/zombies/models/ZombieMale_A.glb`.
Omitting both arguments uses synthetic clip timing and writes no files.

`viewer_zombie_door_collision` covers six zombies following through doors that
open toward/away from the crowd, closing/reopening under force, idle obstruction,
and passage/opening after the obstruction clears. It checks zero body/panel
overlap, identical position/angle replays, 10/30/60/144 Hz opening attempts at the
player interaction's maximum force, corpse proxy removal, floor separation and
door-local selection with 10,000 remote
zombies. Boxes remain outside the proxy collision filter. Optional CSV position,
orientation, animation frame, door angle and overlap logs:
`./build/test_zombie_door_collision build/zombie-door-trace`.

`viewer_zombie_attack` covers each independently exposed body point, solid cover,
3D range/FOV, dead attackers, pre/post-movement attack transitions, stale sight
and attack caches, and a physical door opening/closing/reopening. It verifies
immediate uncached hit rejection, 10 Hz cadence at 30/60/144 FPS, deterministic
ID staggering, no catch-up bursts, and fresh checks when entering range.

`viewer_character_step` runs both controllers against raised road sheets,
downward-facing building caps and solid curbs at 2, 8.7, 25, 30, 31 and 45 cm.
It verifies ascent/descent, standing stability, running at multiple frame rates,
headroom, thin walls behind steps, placement-query compatibility and crowded
foundation traversal. An optional read-only actual-map check takes a path and
world X/Z: `./build/test_character_step output/godollo.glb X Z`.

`viewer_zombie_visibility` compares grouped camera/shadow masks and stable IDs
against a full individual-frustum scan. It covers inside/outside/boundary groups,
negative cell coordinates, geometry crossing cell borders, movement, removal,
group-slot reuse before a query, pose expansion/shrinkage, randomized views, and
a 10,000-instance fixture. It runs without a GPU and checks that small views
avoid population-wide individual tests and unchanged groups do not refit.

`viewer_zombie_shooting` compares prepared head regions and indexed target
selection against the original full-mesh/full-population path. It covers
head-only hits, descendant/blended weights, clip interpolation and wrap,
transformed rigs, fallback head regions, equal-hit ordering, removal/rebuild,
long rays, and a synthetic 10,000-zombie crowd. It runs without a GPU and reports
candidate/pose/vertex counts; its elapsed-time output is diagnostic rather than
a timing assertion.

To inspect all installed character rigs in idle (front row), walk (middle), and
run (back), and verify their sampled skeletons match the displayed baked vertices:

```bash
./build/test_zombie_renderer --assets assets/zombies/models build/zombie-clips.ppm
```

When `output/godollo.glb` exists at configure time, CTest also runs the full-town
FPS integration test. It validates spawning, a real building wall and diagonal
slide, recovery from a requested spawn inside a building, and 4,800 controller
updates across three routes. Every update checks terrain penetration and
building exclusion. The test prints triangle/building/BVH counts, collision
build time, and average controller time per update. Run it directly with:

```bash
./build/test_fps_integration output/godollo.glb
```

The optimized CPU integration run in this workspace built 194,899 collision
triangles (104,331 terrain/road and 90,568 building triangles), representing
2,125 buildings in 65,535 BVH nodes, in about 121 ms. The three town routes
averaged 0.0052 ms per controller update at an average simulation rate of 120 Hz;
this timing includes collision queries and excludes rendering and test assertions.
Both the town and transformed sample-GLB integration checks passed. These are
local measurements, not a guaranteed frame rate on other machines.

The FPS extension was also built and tested through all seven CTest suites.
A native GPU run on AMD Radeon 660M / OpenGL 4.6 Core verified F1/F2 switching,
the 1.70 m eye offset, Space jump and landing, and the existing free-fly input
callbacks. The walking capture is saved as `build/godollo-fps.png`. A separate
three-frame GPU run at `--spawn 10 10 --collision-debug` verified rendering of
the collider overlay; its capture is `build/godollo-fps-debug.png`.

For a finite render run that also exercises the input callbacks:

```bash
./build/godollo_viewer --frames 3 --self-test-input --screenshot build/test.ppm
```

`viewer_player_stance` tests planted-foot crouching, toggled prone, low-ceiling
clearance and recovery, airborne camera-path preservation, delayed leg extension,
and zombie perception behind cover. A synthetic 1.15 m platform blocks a normal
jump but permits a tucked jump at 30, 60, and 144 render updates per second.

Exercise walking mode, mode switches, jump input, Ctrl crouch, prone key events,
and the collision overlay with
the actual GLFW callbacks:

```bash
./build/godollo_viewer output/godollo.glb --self-test-fps \
  --collision-debug --screenshot build/godollo-fps.ppm
```

Capture with a hidden window while keeping the normal graphics driver:

```bash
./build/godollo_viewer --hidden --frames 3 \
  --screenshot build/test.ppm
```

Hidden capture still requires an accessible desktop display. If the environment
has no X11/Wayland display, compile the application and run the CPU tests, then
perform graphical validation on a desktop.
`--allow-software` permits a software OpenGL implementation only for explicit
diagnostic use. It does not establish hardware acceleration. Normal desktop
launches reject recognized software renderers; check the printed GPU name to
confirm the active driver.

The original viewer verification in this workspace covered CMake configure and
build; the camera and loader CTest suites;
native rendering on **AMD Radeon 660M**, Mesa OpenGL **4.6 Core**, GLSL **4.60**;
the generated test asset (7 instances / 38 drawn triangles) and full Gödöllő
scene (4,255 instances / 194,899 triangles / one embedded image). Both captures
contained rendered geometry, and the registered WASDQE, mouse, cursor capture,
resize, and Escape callbacks passed the input check. Screenshots are in
`build/test-native.png` and `build/godollo-native.png` for this verification run.

## Project layout

```text
CMakeLists.txt              Viewer and unit-test build configuration
src/main.cpp               Window, input callbacks, render loop, CLI
items/*.toml               Four prototype weapon configurations
inventory.toml             Starting weapons, loaded rounds, and ammo reserves
src/weapons.{hpp,cpp}       TOML loading, trigger/cooldown state, ammo, and pellet rays
src/weapon_overlay.{hpp,cpp} Batched camera-local firearm meshes
src/weapon_view_pose.{hpp,cpp} Eye-relative hand and shoulder weapon placement
assets/weapons/models.json  CC0 Durango mesh source and modified M14 sniper
tools/build_weapon_assets.py Offline GLB builder for firearms and the HE grenade
tools/prepare_weapon_models.py Blender extraction and sniper modification
src/renderer.{hpp,cpp}      OpenGL resources, shaders, drawing
src/renderer_shadows.cpp   Sun caster pass and lighting uniforms
src/tree_canopy.{hpp,cpp}  Static neighbor canopy cover and tree brightness
src/tree_renderer.cpp      Shared impostor upload and instanced rendering
src/forest_layer.{hpp,cpp}  Runtime forest config loading and layer assembly
src/forest_geometry.{hpp,cpp} Polygon walls/canopies with concavities and holes
src/forest_placement.{hpp,cpp} Repeatable per-patch tree candidates
src/forest_renderer.cpp    Forest visibility and distance/scope detail choice
src/forest_tree_visibility.{hpp,cpp} Patch-first individual-tree visibility
src/forest_volume_batch.{hpp,cpp} Static volume geometry grouped by material
src/file_fingerprint.{hpp,cpp} Shared SHA-256 provenance checks
src/sun_lighting.{hpp,cpp} Lighting settings and stable local shadow coverage
src/day_night.{hpp,cpp}    Game clock and smooth day/night lighting evaluation
src/atmosphere.hpp         Distance haze, ground fog, and matching sky color
src/night_lighting.hpp     Night ambient, sky/fog color, and camera flashlight settings
src/rain_renderer.{hpp,cpp} Instanced rain seeds, draw, and GL state ownership
src/shader_program.{hpp,cpp} Shared scene/rain shader compilation and linking
src/shadow_map.{hpp,cpp}  Depth-only shadow framebuffer and texture ownership
src/flashlight.{hpp,cpp}   Held-light pose and short-range perspective shadow volume
src/animated_model.{hpp,cpp} Idle glTF animation sampling and bind-pose skin data
src/zombie_layer.{hpp,cpp}  Shared character loading, crowd assembly, and shooting
src/zombie_hit.{hpp,cpp}    Conservative shot bounds and fallback head regions
src/zombie_hit_capsules.{hpp,cpp} Prepared bone-capsule loading and affine ray tests
src/zombie_damage.{hpp,cpp} Sparse injury records and health reduction; no geometry or physics
src/zombie_dismemberment.{hpp,cpp} Joint-hit definitions, prepared mesh loading and affine joint tests
src/zombie_shooting.cpp    Prepared hit geometry, spatial bounds, and shot selection
src/zombie_behavior.{hpp,cpp} Dense behavior pool, perception and state transitions
src/zombie_attack.{hpp,cpp} Fresh range/FOV/body-exposure validation for attacks and future hits
src/zombie_perception.{hpp,cpp} Simulation-thread sight scheduling and ray classification
src/zombie_spatial_index.{hpp,cpp} Stable-ID spatial grid for activation, hearing, and shooting
src/zombie_door_collision.{hpp,cpp} Nearby living capsules blocking hinged door motion
src/noise.{hpp,cpp}        Noise events, source levels, and distance attenuation
src/player_noise.{hpp,cpp} Fixed-step footsteps and landing noise emission
src/simulation_clock.hpp   Global simulation-thread in-game clock
src/zombie_chase.{hpp,cpp}  Terrain-following locomotion and clip clocks
src/zombie_crowd.{hpp,cpp}  Deterministic swept body constraints and fine crowd grid
src/zombie_movement.{hpp,cpp} Scene/crowd-constrained capsule motion and terrain support
src/zombie_animation.{hpp,cpp} Authored clip selection and on-demand displayed poses
src/zombie_lod.{hpp,cpp}    Camera detail, distance cutoff and shadow detail policy
src/zombie_lod_assets.cpp   Local LOD manifest, fingerprint and rig validation
src/zombie_lod.py          Rig-preserving offline mesh preparation
src/zombie_placement.{hpp,cpp} Ground queries and deterministic safe placement
src/zombie_park.{hpp,cpp}  Palace garden outline in the published map's frame
src/physics_world.{hpp,cpp} Shared Box3D world and fixed simulation stepping
src/physics_scene.{hpp,cpp} Lazy static triangle collision around moving props
src/box_layer.{hpp,cpp} Dynamic prop bodies, capsule contacts, support and pushes
src/interior_objects.{hpp,cpp} Once-per-building random interior placement
src/box_renderer.cpp       Shared cube instances and current-pose visibility
src/door_geometry.{hpp,cpp} Runtime extraction of independent door panels
src/door_layer.{hpp,cpp} Local hinge activation, force API and door poses
src/door_interaction.{hpp,cpp} Combat/interaction mode and mouse/player door grabs
src/player_interaction.{hpp,cpp} Nearest door/box picking and shared input routing
src/object_interaction.{hpp,cpp} Independent hand grips, strength and release throws
src/door_collision.{hpp,cpp} Doorway clipping and live Box3D capsule contacts
src/zombie_ragdoll.{hpp,cpp} Box3D ragdoll simulation and simplified ground planes
src/zombie_ragdoll_pose.{hpp,cpp} Animated-pose handoff and physics-to-skin mapping
src/zombie_palette_cache.{hpp,cpp} Persistent GPU palettes and coalesced dirty uploads
src/zombie_visibility.{hpp,cpp} Dynamic group bounds and camera/shadow culling
src/zombie_render_updates.{hpp,cpp} Deduplicated instance changes and render publication cursors
src/zombie_renderer.cpp    Shared animation buffers, skinning data, and GPU instancing
src/camera.{hpp,cpp}        Perspective free-fly camera
src/camera_rig.{hpp,cpp}    Runtime camera mode switching
src/audio.{hpp,cpp}         OpenAL playback, listener, voice limits and streaming
src/audio_wave.{hpp,cpp}    PCM16 WAV parsing and incremental decoding
src/scene_audio.{hpp,cpp}   Camera/gameplay playback routing and zombie selection
src/zombie_audio.{hpp,cpp}  Deterministic reaction, moan and footstep scheduling
src/door_audio.{hpp,cpp}    Physics-pose slam detection without changing hinges
src/fps_controller.{hpp,cpp} Fixed-step walking, gravity, jump and contact response
src/player_stance.cpp      Player stance dimensions, clearance and airborne leg tuck
src/collision_world.{hpp,cpp} World-space triangle BVH and capsule/ground queries
src/character_step.{hpp,cpp} Shared bounded capsule step-up and landing checks
src/collision_debug.{hpp,cpp} Optional OpenGL collision line overlay
src/model.{hpp,cpp}         glTF loading and scene data
shaders/basic.{vert,frag}   GLSL 330 sun/ambient lighting and shadow passes
shaders/debug.{vert,frag}   GLSL 330 collision lines
tools/create_test_asset.py Reproducible test-model generator
tools/create_audio_assets.py Synthesized landing and ambience in build/audio/
tools/prepare_audio_assets.py Hash-checked recorded sound preparation
assets/audio/              Curated sound effects, credits and source manifest
assets/test.glb            Generated test model, ignored by Git
tests/test_camera.cpp      Camera checks
tests/test_camera_rig.cpp  Free-fly/walking mode switching and look-pitch checks
tests/test_player_stance.cpp Crouch/prone clearance, airborne tuck and perception checks
tests/test_model.cpp       Loader checks
tests/test_collision.cpp   Synthetic collision checks
tests/test_fps.cpp         Synthetic walking-controller checks
tests/test_fps_integration.cpp Full generated town collision/walking checks
build/                    Generated files, dependencies, and executable
```

The existing Python source, tests, `data/`, and `output/` continue to serve the
scene generator. Generated GLB assets and build output remain ignored by Git.

Dependency license notices remain in the downloaded source trees: GLFW's
`LICENSE.md` (zlib/libpng), GLM's `copying.txt` (MIT or Happy Bunny), and
tinygltf's `LICENSE` (MIT). tinygltf bundles nlohmann/json (MIT) and stb_image
(MIT or public domain), with their notices in the corresponding headers.
Preserve the applicable notices when redistributing dependency code or binaries.
