# Gödöllő scene generator

A functioning Python prototype that downloads public geospatial data and builds
a geographically recognizable, meter-scale approximation of central Gödöllő,
Hungary. It exports terrain, roads, buildings, materials, and an embedded ground
texture as **`output/godollo.glb`**, ready for a separate glTF renderer or Blender.

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

```bash
# Rebuild strictly from local files, with no network requests:
.venv/bin/python -m src.generate --offline

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
"bbox": [19.342, 47.588, 19.369, 47.606]
```

This covers the Royal Palace and central streets. The projected terrain envelope
measures approximately **2,072 × 2,043 meters**. OSM geometry is clipped to the
projected geographic rectangle; the terrain uses its slightly larger, axis-aligned
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
| `roads.lane_width_m` | 3 | Lane-based road-width estimate |
| `roads.sample_spacing_m` | 8 | Road surface subdivision size |
| `roads.offset_m` | 0.25 | Small road clearance above terrain |

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
  roads.py                Width inference, buffered terrain-following road meshes
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
  multipolygon relations, roads, and separately mapped paths/footways with
  tags and geometry. Query results and their source timestamp are cached.

## Geometry behavior

Elevation is bilinearly resampled onto the projected grid, lightly smoothed, and
triangulated with upward normals. Sparse missing elevations are filled from
nearby valid pixels; extreme isolated spikes are corrected. An input with more
than 25% missing target pixels fails clearly instead of creating a fictitious
surface. Building and road heights sample the actual terrain triangle planes.
Terrain UVs align the north-first RGB image to the same projected extent.

Building height precedence is explicit `height`, then `building:levels` times
floor height, then a building-type estimate, then the configured default. Explicit
height includes the roof. Meter and common feet/inches tags are understood.
Buildings below 2m² are omitted. Walls follow the footprint, including courtyard
holes, with foundations below sampled ground and level eaves above the highest
boundary elevation. Supported roof shapes are flat, gabled, and hipped; missing
tags give residential buildings pitched roofs and other types flat roofs.
Unsupported roof shapes use flat roofs. Pitched roof planes follow an oriented
footprint rectangle and are clipped to the actual outline, preserving courtyards.
Small geometry slivers are removed at glTF's float32 precision.

Road width precedence is `width`, then `lanes` times lane width, then highway
class. Major, secondary, residential, service, track, and footway categories are
included. Buffered centerlines are subdivided into small cells and projected
onto terrain with a slight clearance. Interior samples correct small ridge
intersections. Surface tags select reusable asphalt, gravel, dirt, or paving-stone
materials. Segments share one mesh per material to keep the scene compact.
Road counts refer to accepted clipped centerline parts, not triangles.

## Verification and limits

The supplied area has been downloaded and exported end to end. The exporter
checks GLB 2.0 headers and buffer bounds, embedded images, finite coordinates,
triangle indices and degenerate faces, and reloads the file through Trimesh.
Each exported building's walls and roof are also checked together for closed
topology, consistent face winding, and positive volume after float32 conversion.
The regression tests cover roof closure and courtyards, height and width
precedence, multipolygon assembly, slopes and road clearance, raster caching,
offline behavior, terrain node alignment and texture UV round-tripping.
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
at the selected boundary. Roads have simple intersections, no lane markings or
engineered grades, and no bridge/tunnel/layer modeling. Overlapping road surfaces
can meet imperfectly. Railways, trees, fences, facades, interiors, collision
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
