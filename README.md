# Gödöllő scene generator and desktop viewer

A functioning Python prototype that downloads public geospatial data and builds
a geographically recognizable, meter-scale approximation of central Gödöllő,
Hungary. It exports terrain, roads, railway paths, fences, buildings, materials,
and an embedded ground texture as **`output/godollo.glb`**, ready for a separate glTF
renderer or Blender.

The repository also includes a **C++17 Linux desktop viewer** with GLFW,
OpenGL 3.3 Core, textures, free-fly and first-person walking cameras. See
[desktop viewer setup and controls](docs/VIEWER.md) for Linux packages and details.

```bash
cmake -S . -B build
cmake --build build -j
./build/godollo_viewer assets/test.glb
./build/godollo_viewer output/godollo.glb
./build/godollo_viewer output/godollo.glb --fps
```

With the [zombie pack installed](assets/zombies/README.md), the town also loads
1,000 GPU-instanced characters with looping idle animations. Use `--zombie-view`
to frame the crowd or `--no-zombies` to disable it; see the
[crowd controls](docs/VIEWER.md#idle-zombie-crowd) for placement options.

[OpenAL Soft audio](docs/AUDIO.md) follows the camera, plays movement and weapon
effects, streams ambience, and prioritizes nearby zombie voices. The build creates
synthesized placeholder sounds; use `--audio-dir DIR` to supply replacements or
`--no-audio` to mute playback without changing AI hearing.

**F1** selects free-fly; **F2** places a walking player on nearby clear ground.
In walking mode, use **WASD** to walk, the mouse to look, **Space** to jump, and
**Shift** to run. **F3** toggles collision lines and the player capsule; green
means grounded, red means airborne, and cyan marks the ground normal. The
window title also shows the camera/control modes and grounded state. **Tab**
switches Combat/Interaction controls. Combat retains shooting and mouse look;
Interaction lets you hold **left mouse** on a door within 1.5 m and drag or move
with WASD to push/pull it, keeping camera orientation fixed until release.
**Left Alt** releases/captures the mouse; **Escape** closes the viewer.

The walking player is a 1.80 m capsule with radius 0.30 m and eye height 1.70 m.
Gravity, ground contact, and wall sliding use collision triangles derived from
the loaded scene, accelerated by a BVH. Ground slope is limited to 45°; looking
up or down cannot change horizontal walking speed or cause flight. The clear
configuration struct is in `src/fps_controller.hpp`. See the
[walking-mode details and limitations](docs/VIEWER.md#first-person-walking) for slope,
step, spawn, collision, and debugging behavior. No separate collision file is
required.

Tree detector CSVs can also be converted into a separate placement layer using
the shared tree and pine assets. See [tree data preparation](docs/TREES.md) for
merging CSVs, matching source GeoTIFFs, coordinate conversion, and producing
`output/trees.instances.json`. The viewer automatically loads a matching layer
beside the map, renders shared assets with GPU instancing and frustum culling,
and uses simple trunk colliders for walking. See [tree rendering controls](docs/VIEWER.md#trees).

Mapped streetlights, utility poles and electricity pylons can be added to an
existing map as grey rectangular columns, batched by spatial cell:

```bash
.venv/bin/python tools/prepare_pole_instances.py
./build/godollo_viewer output/godollo.glb
```

The first preparation downloads a small independent OSM extract; subsequent
runs can use `--offline`. Preparation also places muted green HÉV mast columns
approximately every 50 m between paired main tracks, using the map's cached
railway extract. The viewer automatically loads the fingerprinted
`output/poles.instances.json` layer. See [pole preparation and controls](docs/VIEWER.md#mapped-pole-columns).

Roadside guardrails can be prepared independently with
`.venv/bin/python tools/prepare_guardrails.py`. The viewer automatically loads
the matching `output/guardrails.glb`: spatially batched, double-sided strips
with the rail and posts painted into one alpha texture. See
[guardrail preparation and controls](docs/VIEWER.md#mapped-guardrails).

The sections below document the Python scene generator.

## Setup and run on Linux

Use Python **3.11 or 3.12**; this project was tested with Python 3.12 on Linux.
The pinned packages have Linux wheels, including their GDAL, PROJ, and GEOS
libraries. A separate GDAL installation, Blender, GPU, and cloud account are not
required. Debian/Ubuntu may need the `python3-venv` system package to create the
virtual environment.

From this project directory:

```bash
mkdir -p data/tmp data/cache/pip
export TMPDIR="$PWD/data/tmp"
export PIP_CACHE_DIR="$PWD/data/cache/pip"
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements.txt
.venv/bin/python -m src.generate
```

The one generation command downloads missing inputs, processes the rasters,
builds geometry, exports the GLB, reloads it for verification, and writes
`output/report.json` with counts, bounds, source provenance, and validation
results. It exits with a nonzero status and an explanatory message on failure.

Subsequent runs reuse cached source data. No credentials are needed for any of
the three public endpoints used by the prototype. The initial run needs HTTPS
access to Overpass, Earth Search, and public AWS COGs. Overpass may be busy; the
downloader retries and tries alternate endpoints.

Fence support expands the OSM query, so older building/road/railway extracts
cannot be reused for it. Run `.venv/bin/python -m src.generate --fences-only`
once with network access to update an existing map. Full generation also fetches
the expanded extract and reuses existing DEM and Sentinel caches. Subsequent
runs can use `--offline` again.

```bash
# Rebuild strictly from local files, with no network requests:
.venv/bin/python -m src.generate --offline

# Add or update only fences, reusing the published town and terrain:
.venv/bin/python -m src.generate --fences-only
# After the expanded OSM extract has been downloaded:
.venv/bin/python -m src.generate --fences-only --offline

# Explicitly fetch fresh OSM, catalog results, and raster crops:
.venv/bin/python -m src.generate --refresh

# Use another configuration or inspect a detailed error traceback:
.venv/bin/python -m src.generate --config config.local.json --verbose

# Inspect the completed GLB without a renderer:
.venv/bin/python -m src.verify output/godollo.glb

# Run offline regression tests:
.venv/bin/python -m unittest discover -s tests -v
```

## Configuration and coordinates

Edit `config.json`. Its geographic bounding box uses **longitude, latitude** in
the order `[west, south, east, north]`:

```json
"bbox": [19.31642, 47.570566, 19.39458, 47.623434]
```

This covers the Royal Palace, central streets, and surrounding neighborhoods.
The projected terrain envelope measures approximately **6,000 × 6,000 meters**.
OSM geometry is clipped to the projected geographic rectangle; the terrain uses
its slightly larger, axis-aligned
projected envelope. Raster downloads include a small padding for interpolation.

All geometry is calculated in **WGS 84 / UTM zone 34N (`EPSG:32634`)**, subtracting
the projected bounding-box center. Longitude **19.3555**, latitude **47.597** is
the local horizontal origin. The terrain elevation at that point is subtracted
from all heights, so the ground at the center is `(0, 0, 0)`. Original elevation
values and the vertical offset are recorded in the report. Copernicus elevations
use the EGM2008 vertical datum.

Internally, coordinates are east/north/up. A root transform exports a standard
right-handed glTF scene: **X = projected east, Y = up, Z = projected south**.
One unit equals one meter. `report.json` and glTF scene extras retain the CRS,
projected origin, geographic origin, and elevation offset needed to georeference
the model again. A conforming renderer applies the scene's node transforms.

Other settings include:

| Setting | Default | Effect |
| --- | --- | --- |
| `terrain.spacing_m` | 15 | Terrain mesh node spacing; the DEM itself is about 30m resolution |
| `terrain.smoothing_sigma` | 0.6 | Gentle Gaussian smoothing in grid cells; use 0 to disable |
| `terrain.texture_size` | 512 | Square RGB texture size; larger images add no source detail |
| `sentinel.datetime` | May–September 2025 | Search interval for a summer ground-color scene |
| `sentinel.max_cloud_cover` | 20 | Maximum scene-wide cloud percentage |
| `buildings.floor_height_m` | 3 | Floor height used when OSM has levels but no height |
| `buildings.default_height_m` | 8 | Final building-height fallback |
| `buildings.detail_distance_m` | 120 | Range for walls with door/window openings; scope magnification extends it |
| `roads.lane_width_m` | 3 | Lane-based road-width estimate |
| `roads.sample_spacing_m` | 8 | Road surface subdivision size |
| `roads.offset_m` | 0.02 | Base road clearance above terrain; ridge corrections can increase it |
| `railways.width_m` | 4 | Track-bed width when an OSM `width` tag is absent |
| `railways.sample_spacing_m` | 8 | Railway surface subdivision size |
| `railways.offset_m` | 0.3 | Track-bed clearance above terrain |
| `fences.default_height_m` | 1.5 | Fence height when OSM has no usable `height` |
| `fences.post_spacing_m` | 2.5 | Textured post interval; independent of panel geometry |
| `fences.default_gate_width_m` | 3 | Clear gap at mapped gates/entrances without a `width` |

The prototype limits regions to 10km per side and terrain grids to two million
cells. Start with the supplied area. For distant locations, choose an appropriate
projected CRS with meter units as well as a different bounding box.

## Data and local storage

All downloaded data, request records, caches, and temporary files stay **inside
this workspace**. Processing reads local files only. Source files are cached by
bounds and acquisition settings. Existing inputs are validated and reused;
changing source settings creates a new cache entry, and `--refresh` replaces the
relevant entries. OSM snapshots remain unchanged until explicitly refreshed.

```text
config.json
requirements.txt
src/
  generate.py             Pipeline, CLI, export, report
  config.py               Configuration validation and projected coordinate frame
  data.py                 Source queries, downloads, and cache validation
  terrain.py              Elevation resampling, heightfield, texture and UVs
  osm.py                  Ways, multipolygon relations, holes, clipping
  buildings.py            Height inference, walls, flat/gabled/hipped roofs
  building_details.py     Road-facing entrance and window layout
  wall_openings.py        Cut facade outlines and index wall/door/glass vertices
  facade_surface.py       Preserve sampled facade and foundation boundaries
  building_caps.py        Refine bottom caps at door-base corners
  roads.py                Width inference, buffered terrain-following road meshes
  railways.py             Terrain-following railway track-bed meshes
  terrain_surfaces.py     Shared surface subdivision and terrain clearance
  verify.py               GLB structural and geometry inspection
tests/
data/
  raw/
    osm/                  Overpass JSON, query, request and source metadata
    copernicus_dem/       Native-resolution elevation crops and metadata
    sentinel/             STAC search/items, native RGB crop and metadata
  processed/
    terrain_elevation.tif Absolute elevations in the projected grid
    terrain.npz           Local height grid, bounds and elevation offset
    sentinel_rgb.tif      Projected RGB texture with georeferencing
    terrain_texture.png   Ground texture embedded in the GLB
  tmp/                    Workspace-local scratch space
  cache/                  Optional dependency download caches
output/
  godollo.glb             Self-contained scene
  report.json             Counts, dimensions, provenance and verification
  ATTRIBUTION.md          Data notices to keep with the exported scene
```

`data/`, generated `output/` files, `.venv/`, and local config/secrets are ignored
by `.gitignore`; source, configuration, tests, and documentation can be committed.

* **Copernicus DEM GLO-30:** anonymous HTTPS access to the
  [public AWS distribution](https://registry.opendata.aws/copernicus-dem/).
  The downloader addresses the appropriate one-degree tiles, reads only the
  required COG byte ranges, and stores native pixels as a georeferenced GeoTIFF
  subset. Raw crops retain source elevations; no huge full tile is needed.
* **Sentinel-2 Level-2A:** the free
  [Earth Search catalog](https://github.com/Element84/earth-search) identifies
  low-cloud scenes covering the padded area. The public
  [Sentinel COG archive](https://registry.opendata.aws/sentinel-2-l2a-cogs/)
  provides the 10m true-color RGB `visual` asset. Native pixels are cached first,
  then cropped/reprojected/resampled to the terrain texture. The initial run
  selected **`S2A_34TCT_20250813_0_L2A`**, acquired August 13, 2025.
* **OpenStreetMap:** a small Overpass query obtains building ways, building
  multipolygon relations, roads, separately mapped paths/footways, railway
  track ways, fences and entrance nodes with tags and geometry. Query results
  and their source timestamp are cached.

## Geometry behavior

Elevation is bilinearly resampled onto the projected grid, lightly smoothed, and
triangulated with upward normals. Sparse missing elevations are filled from
nearby valid pixels; extreme isolated spikes are corrected. An input with more
than 25% missing target pixels fails clearly instead of creating a fictitious
surface. Building, road, railway and fence heights sample the actual terrain
triangle planes. Terrain UVs align the north-first RGB image to the same projected extent.

Building height precedence is explicit `height`, then `building:levels` times
floor height, then a building-type estimate, then the configured default. Explicit
height includes the roof. Meter and common feet/inches tags are understood.
Buildings below 2m² are omitted. Walls follow the footprint, including courtyard
holes, with foundations below sampled ground and level eaves above the highest
boundary elevation. Supported roof shapes are flat, gabled, and hipped; missing
tags give residential buildings pitched roofs and other types flat roofs.
Unsupported roof shapes use flat roofs. Pitched roof planes follow an oriented
footprint rectangle and are clipped to the actual outline, preserving courtyards.
Roof triangulation preserves sampled boundary and ridge edges, including nearly
collinear points, so precision rounding does not leave disconnected roof pieces.
Collapsed faces are removed at glTF's float32 precision.

The original closed walls remain the lower LoD. At higher detail, an alternative
wall mesh has real rectangular door and window openings, filled flush with
brown door panels and blue glass at 60% transparency (40% opacity). Masonry is
removed behind each panel. The roof is shared between both LoDs. Detailed walls,
doors, glass and roofs are double-sided so the empty building shell is visible
through glass.
Doors have runtime physics hinges that can swing outward under forces or rigid-body
contacts; player interaction and modeled rooms are not implemented. See
[the viewer guide](docs/VIEWER.md) for the physics API.

The front door uses the exterior wall closest to the nearest road. Among walls
within 0.5 m of the closest wall, prefer the outward normal facing most directly
toward the road from the wall center. A second door uses the opposite wall.
Windows cover exterior and courtyard walls, fit below the eaves, and leave room
around doors. Very short or low walls omit rectangles that cannot fit. With no
road, the longest exterior wall supplies the front. The lowest window row and
both doors share a head height 2.1 m above the highest sampled foundation ground.
Door bottoms follow the actual wall base, including slopes; the original bottom
cap is 8 cm below terrain. On slopes, door heights vary with that base.
Each whole facade outline is cut before triangulation, preserving roof and
foundation samples without carrying the original wall diagonals through glass.
Below the lowest sill, vertical strips retain the sampled foundation profile.
Only the detailed bottom cap is refined at new door-base corners. Within each
material mesh, vertices with matching exported positions and normals are
shared, keeping corners sharp.
Both complete shells are checked for closure and consistent winding after GLB
export. Each building batches detailed masonry, doors and glass into at most
three extra meshes.

Version 2 `building_detail` mesh extras store `distance_m`, a unique building
`group`, and `level` (`near` or `far`). All alternatives use shared transformed
building bounds to switch together. The viewer also supports version 1 overlay
maps and original maps. Other glTF viewers need to choose one wall alternative.

The desktop viewer selects higher detail within `buildings.detail_distance_m`
(default 120 m). Both scope views extend this distance according to magnification
and retain frustum culling. Walking collision has door openings and tests the
capsule against each panel's current physics geometry: you can pass when the
panel leaves sufficient clearance. Windows still block walking, and building
shadows retain the original closed shell. Regenerate with
`.venv/bin/python -m src.generate --offline` to add them to an existing map;
previous maps continue to load with their original appearance. Regenerating a
map changes its fingerprint, so rebuild any tree placements as described in
[the tree preparation guide](docs/TREES.md).

Road width precedence is `width`, then `lanes` times lane width, then highway
class. Major, secondary, residential, service, track, and footway categories are
included. Buffered centerlines are subdivided into small cells and projected
onto terrain with a slight clearance. Interior samples correct small ridge
intersections. Surface tags select reusable asphalt, gravel, dirt, or paving-stone
materials. Segments share one mesh per material to keep the scene compact.
Road counts refer to accepted clipped centerline parts, not triangles.

Fences use explicit OSM `barrier=fence` ways, including closed loops; property
and land-use outlines do not imply fences. OSM `height` accepts metres and common
feet/inches units, falling back to `fences.default_height_m`. `fence_type` and
`material` choose chain-link, metal railing, wood or masonry approximations;
unknown types use chain-link. Wire/railing panels have embedded, double-sided
alpha masks, while wood and masonry panels are solid. Posts are painted into
the panel textures with no separate post geometry. Panels share meshes by
material and split at bends and terrain slope changes to follow the ground
without gaps. Straight stretches over a constant slope use one quad (two
triangles), merging redundant mapped vertices and terrain edges. Post spacing
controls the texture repeats without adding panel subdivisions.

Mapped gate, entrance, lift/swing gate, kissing gate and stile nodes belonging
to a fence way create physical gaps before the fence is clipped to the map.
Explicit `width` wins; otherwise gates/entrances use the configured width and
kissing gates/stiles use 1.2 m. Gate leaves and access restrictions are not
modeled; these passages remain open. Continuous panel triangles block walking,
including across transparent wire texels. The report and glTF scene extras
include accepted fence-way counts, clipped segments, length and entrance gaps.
Coverage depends on OSM mapping; unmapped fences are absent.

`--fences-only` updates an existing `output/godollo.glb` without reconstructing
buildings, roads or railways. It samples the published terrain, validates the
candidate and unchanged terrain, and keeps map/report backups under
`output/fence-backups/`. No DEM or satellite source caches are required.
Repeated updates replace existing fence meshes while retaining the town's
binary payload. Old fence buffers remain unused; a full rebuild compacts them.
Original town OSM provenance is retained; the fence extract is recorded separately under
`sources.fences`. Either generation mode changes the map fingerprint: rebuild
tree placements and revalidate forest polygons against the updated map before
refreshing their fingerprints.

Railway paths include OSM [`railway=rail`, `light_rail`, and `narrow_gauge`](https://wiki.openstreetmap.org/wiki/Key:railway)
ways, covering HÉV and MÁV tracks without requiring an `operator` tag. Active
sidings, spurs and crossovers are included; platforms and proposed, abandoned,
or disused tracks are excluded. Route relations do not duplicate their track
ways. Each mapped track becomes a textured ballast surface, with an explicit OSM
`width` overriding `railways.width_m`. Parallel track ways remain separate;
`tracks` and `passenger_lines` do not multiply the width. Buffered edges are
clipped to the configured area and follow the same terrain sampler and clearance
correction as roads. The surfaces share the `railways_ballast` mesh and
`railway_ballast` material, with OSM IDs retained in mesh extras. GLB scene extras
and `output/report.json` include a `railways` section with accepted segment,
category, mesh, triangle and skipped-feature counts. Railway-free areas remain
valid. The shared [railway tile](assets/railways/README.md) contains steel rails
with a 1.435 m clear gauge and timber ties every 0.60 m, with gravel between them.
UVs follow each centerline and continue tie spacing across joined ways; bed width
changes the gravel shoulders, not the gauge. All included railway categories
currently use this standard-gauge artwork, including `narrow_gauge` ways.

Apply the texture to an existing published map without downloading data or
regenerating the town:

```bash
.venv/bin/python tools/apply_railway_texture.py
```

This uses the cached OSM extract recorded in the map and verifies matching
railway positions, indices, and source IDs. It preserves the original geometry
and backs up the map and adjacent companions under
`output/railway-texture-backups/<original-map-sha256>/`. After validating the
new GLB, it refreshes the tree manifest's map fingerprint while preserving all
tree transforms and asset fingerprints. Restart the viewer to load the texture;
no C++ rebuild is needed. Future generator runs embed the texture automatically.

## Verification and limits

The supplied area has been downloaded and exported end to end. The exporter
checks GLB 2.0 headers and buffer bounds, embedded images, finite coordinates,
triangle indices and degenerate faces, and reloads the file through Trimesh.
Each exported building's walls and roof are also checked together for closed
topology, consistent face winding, and positive volume after float32 conversion.
The regression tests cover roof closure (including GLB round trips of previously
failing 6 km map buildings) and courtyards, height and width
precedence, multipolygon assembly, railway clipping and export, slopes and surface
clearance, raster caching, offline behavior, terrain node alignment and texture
UV round-tripping.
See `output/report.json` for the measurements from the latest successful run.

This is a rough game-level base, not a survey or photogrammetric reconstruction.
The [Copernicus DEM is a surface model](https://copernicus-dem-30m.s3.amazonaws.com/readme.html),
so vegetation and existing structures can affect elevations. Its approximately
30m detail cannot reproduce curbs or foundations; smoothing does not remove all
surface artifacts. Satellite color is approximately 10m detail and includes
shadows and roofs already visible in the image. Cloud ranking is scene-wide,
and the prototype requires one usable Sentinel scene covering the entire padded
area; it does not mosaic scenes or perform local cloud classification.

OSM completeness and optional tags vary. Inferred heights and roof shapes are
approximate, irregular pitched roofs can look stylized, and features may be cut
at the selected boundary. Railway paths are track-bed surfaces; rails and sleepers
are texture detail, while stations and overhead wires are not modeled. Roads have
simple intersections, no lane markings or engineered grades, and no
bridge/tunnel/layer modeling.
Overlapping road surfaces can meet imperfectly. Railway paths also follow terrain
rather than engineered bridge or tunnel elevations. Trees, fences, facades, interiors, collision
meshes, automatic LODs and connected sidewalk generation are outside this
prototype. Dataset acquisition dates differ.

## Attribution and licensing

Retain [ATTRIBUTION.md](ATTRIBUTION.md) when sharing the scene. It includes
OpenStreetMap contributor credit and the ODbL link, the required modified
Copernicus WorldDEM-30 notice, and the modified Copernicus Sentinel data notice.
These notices are also embedded in scene extras and copied to `output/` with
the selected Sentinel year. Freely accessible data retains its source terms;
the project does not relicense it. Source URLs, selected scene metadata and
access timestamps are retained with the raw inputs and in the output report.
