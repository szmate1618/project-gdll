# Vehicle asset preparation

All 14 requested models were downloaded from the official Sketchfab download API.
Both the listing metadata and the bundled license files specify **CC BY 4.0**.
All pass the commercial-use-and-modification license filter; none were excluded.
Preserve the [author credits and modification notice](CREDITS.md) when distributing
assets or derivatives. License evidence is in `license-audit/` and [sources.json](sources.json).

The Fiat archive contains three complete variants, so there are 16 prepared vehicles.
The default budget is 999 triangles. Following approval to increase the budget where
it improves quality, the Corsa, three Fiats, Passat and Swift allow up to 1,500.
Models already below budget retain their geometry.

Open the [comparison overview](comparisons/overview.png), or the individual three-view
comparisons below. Original and reduced geometry use the same camera, lighting and
display scale. The third view overlays triangle edges.

| Vehicle | Downloaded triangles | Selected output | Budget | Comparison |
| --- | ---: | ---: | ---: | --- |
| Toyota Corolla 1999 | 144 | 144 | 999 | [View](comparisons/toyota-corolla-1999.png) |
| Volkswagen Golf V 2006 | 160 | 160 | 999 | [View](comparisons/volkswagen-golf-v-2006.png) |
| Mercedes-Benz S 500 1995 | 198 | 198 | 999 | [View](comparisons/mercedes-benz-s-500-1995.png) |
| Volvo V70 | 214 | 214 | 999 | [View](comparisons/volvo-v70.png) |
| Suzuki Grand Vitara 2000 | 232 | 232 | 999 | [View](comparisons/suzuki-grand-vitara-2000.png) |
| Ford Focus 2005 | 608 | 608 | 999 | [View](comparisons/ford-focus-2005.png) |
| Volkswagen Golf Mk1 1983 | 904 | 904 | 999 | [View](comparisons/volkswagen-golf-mk1-1983.png) |
| Lada 2107 | 1,475 | 995 | 999 | [View](comparisons/lada-2107.png) |
| Opel Astra 2008 | 4,634 | 998 | 999 | [View](comparisons/opel-astra-2008.png) |
| Opel Corsa B | 11,637 | 1,496 | 1,500 | [View](comparisons/opel-corsa-b.png) |
| Fiat 126p Maluch / basic | 3,424 | 1,486 | 1,500 | [View](comparisons/fiat-126p-maluch-basic.png) |
| Fiat 126p Maluch / flat | 3,424 | 1,481 | 1,500 | [View](comparisons/fiat-126p-maluch-flat.png) |
| Fiat 126p Maluch / tuned | 5,096 | 1,482 | 1,500 | [View](comparisons/fiat-126p-maluch-tuned.png) |
| Volkswagen Passat B8 | 12,598 | 1,473 | 1,500 | [View](comparisons/volkswagen-passat-b8.png) |
| Suzuki Swift 2024 | 21,743 | 1,499 | 1,500 | [View](comparisons/suzuki-swift-2024.png) |
| Skoda Octavia 2001 | 4,842 | 996 | 999 | [View](comparisons/skoda-octavia-2001.png) |

Counts are measured from actual triangle primitives, including each active scene
instance. Ford Focus has 608 downloaded triangles versus 612 listed; Corsa has
11,637 versus 11,712 listed; Passat has 12,598 versus 12,623 listed. The Fiat
variants total 11,944 downloaded triangles. [preparation.json](preparation.json)
records output paths, triangle budgets and SHA-256 hashes.

Originals are `models/<name>-original.glb`. Selected outputs are recorded in
`preparation.json`: `-under-1000.glb` for the default budget, and `-optimized.glb`
for the higher budget. Earlier strict-budget candidates for the latter are also
preserved as `-under-1000.glb`; the comparisons show the selected output.

The original and selected GLBs preserve textures and material settings, compact
unused buffers, and omit inactive Fiat variants. meshoptimizer 1.3 adjusts vertex
positions and attributes for reduced meshes. Budget allocation uses rendered
surface areas, accounting for scene-node scales. Original frames and units remain
as authored; the preview applies a display-only rotation and fit.

The Swift official glTF contains identical white materials with no textures.
Its selected output joins those equivalent static parts before simplification to
avoid cracks between independently reduced panels. Their transforms are baked
exactly once; the original GLB retains separate nodes. Even at 1,499 triangles,
the Swift remains visibly coarser than the original. Wheel and panel detail also
remains reduced on the Fiat, Corsa and Passat.

These are deterministic textured CPU previews. They do not simulate normal maps,
environment reflections or PBR specular highlights. The runtime now uses the fitted derivatives described under [vehicle LoDs](#vehicle-lods).

## Reproduce

Reuse the repository Python environment. Downloading requires a Sketchfab API
credential supplied through `SKETCHFAB_API_TOKEN` or `--token-file`; credentials
are never written into the asset manifest or forwarded to the download CDN.

```bash
.venv/bin/python tools/download_vehicle_models.py --assets assets/vehicles --token-file /path/to/token-file
```

Cached archives are checked against recorded hashes and can be reused. To build
and prepare using a fresh build directory:

```bash
cmake -S . -B build/vehicle-assets -DGD_VIEWER=OFF
cmake --build build/vehicle-assets --target vehicle_mesh_simplify -j4
.venv/bin/python tools/prepare_vehicle_models.py --helper build/vehicle-assets/vehicle_mesh_simplify
```

The preparation command is offline. Completed comparisons are reused when their
model hashes, renderer signature and image hash match. Rendering can resume after
interruption. Scene, image and triangle-budget validation precede publication.

`downloads/`, `source/` and `models/` contain local ignored assets. Preparation code,
manifests, credits, license evidence and comparison PNGs are trackable inputs for
review. Source PNGs elsewhere in the repository are unaffected.

## Verification

The CMake `vehicle_mesh_simplify` target was configured and built offline using
cached dependency sources. Four focused regressions cover full parent transforms
and reflections, scene-instance budgets, equivalent-material merging and texture
orientation. Every selected GLB is also checked independently with trimesh for
its actual scene triangle count; source archive and GLB hashes are verified.

## Vehicle LoDs

The preparation command generates a local texture inventory (`LOD_REPORT.md`),
comparison overview (`lod-comparisons/overview.png`), and machine-readable report
(`lod-report.json`). These outputs are excluded from Git.

`tools/prepare_vehicle_lods.py` builds four static levels from each **selected**
prepared mesh, never from an already reduced LoD. LOD0 retains its geometry;
LOD1/2/3 request 65%/35%/15%, with geometric error limits of 1%/2.5%/4%.
Material borders and texture-atlas discontinuities are protected. Redundant
hard-normal splits can collapse within the error limit. Measured counts
can exceed the requested budget; a 144-triangle car needs little simplification.
Images are capped, preserving aspect ratio, at 1024/512/256/128 pixels. Small
textures are never enlarged. The original downloaded and selected files remain
available with their original texture resolutions.

```bash
cmake --build build/vehicle-asset-check --target vehicle_mesh_simplify -j4
.venv/bin/python tools/prepare_vehicle_lods.py --helper build/vehicle-asset-check/vehicle_mesh_simplify
```

Rebuild the helper when updating the preparation code: its private binary
protocol now includes seam protection. Generation is offline and publishes
content-addressed GLBs under `models/lod0/` through `models/lod3/`, followed by an
atomic `models/lods.json` replacement. Existing caches are preserved. Source and
LoD SHA-256 hashes, triangle counts and paths are validated at runtime. Missing,
stale or malformed caches fall back to the procedural car without mixing levels.
GLBs, the runtime publication, LoD reports and LoD previews are ignored.
The generation settings and preparation code are version controlled.

The viewer and multiplayer client share uploaded assets by config ID. At a
60-degree vertical FOV the nominal transitions are 20/50/100 m, with 10% hysteresis;
outward changes occur at 22/55/110 m, inward changes at 18/45/90 m. Scope zoom
retains detail by apparent size. Cars beyond 300 apparent meters are culled;
occupied cars retain LOD0. `--no-vehicle-lod` keeps LOD0 and disables the cutoff in
the standalone viewer. Camera, sun-shadow and flashlight-shadow passes use the
same selected mesh, each with its own frustum. These policies do not change
physics, network updates, entry points, seats, collisions or simulation cadence.
Opaque and masked parts are instanced by type, LoD and source material/primitive.
Each group's rigid transforms stream once per pass through an orphaned instance
buffer. Transparent parts remain individually drawn in global back-to-front order;
the prototype fallback batches chassis and independently posed wheels separately.
Use `--no-vehicle-instancing` to compare individual draws at the same LoD settings.

A refittable BVH combines the apparent-distance region with each pass's own
frustum. It retains off-camera shadow casters and conservative bounds over every
LoD, plus suspended prototype wheels. Pose revisions come from physics motion
and explicit multiplayer poses; cached matrices and spatial bounds update only
when changed. The tree rebuilds when the population changes. A lightweight
revision check still runs once per car each frame; visibility selection no longer
scans every car for every pass. Refits preserve the original tree partition.

`Renderer::vehicleStats()` reports selected levels, camera triangles/draws,
instanced and transparent draw counts, range candidates, BVH node/instance tests,
and changed spatial bounds.

`lod-settings.json` records reviewed source yaw corrections and the initial
uniform fit to the existing prototype: 3.6 m long, tire bottom 0.68 m below the
chassis origin, X right, Y up, -Z forward. Parent transforms and reflections are
baked once; equivalent material draws are joined. Focus/Astra legacy diffuse
materials are converted to base-color input for this shader; imported PBR effects
are not reproduced. The imported meshes include **static wheels**: wheel spin,
steering and suspension are simulated physically but are not separately animated
on these complete-car meshes. The procedural fallback still animates its wheels.
Physics dimensions and configured seats/lights remain prototype tuning; per-model
physical dimensions and detailed interiors need later fitting.

Portable asset ZIPs include only the active LoD publication, its source checksums
and required credit files. The basic Linux/Windows application packages continue
to obtain these optional visual assets from a separately supplied asset pack.

`viewer_vehicle_lod` tests policy and stale/invalid publications without a GPU.
`viewer_vehicle_lod_renderer` loads all 16 types when the local cache exists,
checks distance/zoom/occupied selection and GL state, writes per-type captures,
and checks moving/parked spatial bounds in a 1,000-car population. It also records
a 192-car diagnostic in `build/vehicle-asset-check/vehicle-lod-renderer/`.
The latter returns CTest skip code 77 when assets or a graphics context are absent.
The diagnostic times complete 960×640 render calls with shadows off, six warmup
frames and four alternating 20-frame rounds of near individual, LoD individual,
and LoD instanced drawing. Simulation is not advanced between variants. Blocking timestamp queries
are confined to this diagnostic. Its timings do not isolate texture size from
mesh reduction or predict performance in the generated town.
