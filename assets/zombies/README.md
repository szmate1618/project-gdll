# Polyart Zombies

Ten characters by **Denys Almaral** from the
[Polyart Zombies with Animations Free Pack](https://sketchfab.com/3d-models/polyart-zombies-with-animations-free-pack-d9bcfdd88f5348549bc947226af7c314).
The creator links the [official Three.js demo](https://denysalmaral.com/gamedev/free-zombies/)
in that listing. Its public model files are the source used here. The complete
authoring pack is also available from the creator's
[Fab listing](https://www.fab.com/listings/2a85aa17-6eae-4c9d-b805-aeb1d722962f).

The Sketchfab listing's current public metadata identifies the asset license as
[Free Standard](https://sketchfab.com/licenses). The original creator retains
copyright. Keep the downloaded source assets local; this repository records the
download locations and SHA-256 hashes in `sources.json` rather than redistributing
the model binaries. The listing also carries the creator's NoAI restriction.

From the repository root, download and prepare the runtime files:

```bash
python3 tools/download_zombies.py
```

Rebuild the same GLBs from the cached originals without network access:

```bash
python3 tools/download_zombies.py --offline
```

`source/` preserves the original glTF files, buffers, and shared palette PNGs.
The importer downloads the ten `ZombieFemale_A` through `ZombieFemale_E` and
`ZombieMale_A` through `ZombieMale_E` rigs and their matching `idle_220f`,
`walk_64f`, `running_58f`, `attack_left_70f`, and `attack_right_70f` clips for
each skeleton. `models/` contains one self-contained GLB per character with
all five clips and an embedded palette. Both directories
are ignored by Git. Run the downloader once online to add missing animation
sources to an older cache; subsequent rebuilds can use `--offline`.

The source models use centimeters and Y-up. Preparation adds a parent scale of
0.01 to the whole original scene, preserving mesh transforms, bone hierarchy,
inverse bind matrices, and animation values. The resulting GLBs use meters.
Animation channels are matched by exact node names and require identical
skeleton parent relationships; animation data, buffer views, and accessors are
remapped into each character without replacing its mesh or bind matrices. Clips
keep their original names and separate sampler/accessor ranges. `sources.json`
records hashes for all source clips and the clip names in each runtime model.

Attacks last 2.33 seconds and are baked with their final endpoint for playback
that runs once. The normal shared vertex-animation buffers also contain these
frames; nearby attackers use swept hand spheres for damage. After adding clips,
rebuild LoDs with `python3 tools/prepare_zombie_lods.py`. Existing capsule and
joint-hit definitions keep their geometry: preparation refreshes their source
hash only after reproducing the exact former three-clip GLB they describe.
Other stale collision definitions still fail validation.

The viewer and graphical multiplayer client upload the shared vertex animations
once, including animated cavity closures for the original and severed meshes.
They then call `ZombieLayer::releaseAnimationVertices()` to free CPU vertex frames
and their spare allocation capacity for every mesh level. Bone frames, bind
geometry and skin weights remain available for attacks, hit tests and ragdolls.
Cached animation extents preserve visibility and replication bounds through
spatial-index rebuilds. Head/chest wounds still remove triangles at hit locations
through dynamic replicated masks; their supporting closure geometry is shared.
Recreating a renderer after releasing CPU frames requires reloading the assets.

Downloads and output GLBs are written to temporary files before publication.
Subsequent runs verify cached or downloaded originals against `sources.json`.
A source hash mismatch stops preparation so upstream changes can be reviewed.

`grassalkovich_park.json` records the Royal Palace gardens (Felső park /
Kastélypark) outline from [OpenStreetMap relation 18595054](https://www.openstreetmap.org/relation/18595054),
© OpenStreetMap contributors, [ODbL 1.0](https://www.openstreetmap.org/copyright).
The outer ring is projected into EPSG:32634 meters using `src/config.py`'s
transformer. Coordinates remain absolute; the viewer subtracts the published
map's origin and reverses north into world Z south. The default town crowd
contains 300 zombies spread across safe ground inside this outline. See
[viewer controls](../../docs/VIEWER.md#zombie-crowd) for overrides.

## Joint hits and prepared severed meshes

Each character also has `models/NAME.capsules.json` for ordinary shot collision.
These are prepared asset data, not fitted again at startup. Every skeleton node
has a named record; bones with attached surface geometry have a capsule, while
unused bones and nondeforming helper nodes are disabled. Endpoints and radius
use meters in the reference bone's orientation, following the animated bone
with its original scale compensated. The loader checks the source GLB SHA-256.

Capsules fit the skin-influenced reference surface with a robust near-full
coverage fit (about the 98th percentile plus 2 mm), avoiding large inflation
from a few protruding vertices. Female A and E have skirts: their pelvis/thigh
capsules instead follow estimated underlying anatomy, leaving empty skirt fabric
outside the damage volume. The fit method and surface coverage are recorded in
each capsule entry. No preparation tool or generation workflow is required.

Shooting retains the spatial grid and initial conservative bounding box, then
tests the posed capsules instead of the oversized torso/head spheres. Head and
descendant capsules count as headshots; nearest capsule entry chooses ordinary
damage and occlusion distance. Separate joint spheres below retain precedence
on the same actor. Assets without capsule definitions keep their original
body/head proxy fallback. The capsule files are included in multiplayer asset
packs and content fingerprints, with LF line endings.

Torso hits additionally test the remaining original posed triangles, removing
triangles with a torso vertex within 10 cm of the impact. The visible surface
and an 80%-scale inner collision shell share the original triangle mask.
Three small dark red boxes demonstrate loose contents inside a wounded torso.
Bullet impacts also emit one short-lived textured blood-mist quad: small for
ordinary hits, medium for dismemberment, and large when torso damage opens more
geometry. The effect shares one texture and one instanced batch across hits.
Anatomical torso boundaries receive dark red closures before shot removal;
the camera and inner collider remove matching cap triangles when wounded.
Surviving torso walls expose dark red backfaces, while shadow geometry stays
unchanged. These runtime closures do not reuse the prepared dismemberment caps.
Wounds persist on corpses independently of health; shadow geometry stays intact.
See [runtime torso wounds](../../docs/VIEWER.md) for the collider and LOD behavior.

Each original character has a companion `models/NAME.hitboxes.json`. These
definitions contain ten spherical joint regions: waist, neck, and left/right
shoulder, elbow, hip and knee. Centers and radii use meters in the reference
bone's orientation; the loader compensates for the source rig's centimeter
scale. The file records the attachment bone by name and the corresponding
ragdoll body. The source GLB and every prepared mesh are checked by SHA-256.

A joint hit kills immediately, removes any health injury record, releases the
matching ragdoll connection and selects that joint's prepared corpse mesh.
Both sides of each cut have dark red opaque caps. Skin weights stay on their
own side of the separation; detached upper limbs retain their lower sections.
All pieces use the original skeleton palette, so bone IDs remain stable.
Corpses retain the selected separation independently of the health subsystem.

The hit physics body receives a randomized impulse along the shot ray:
4–8 N·s for low caliber and 10–16 N·s for high caliber. Non-head parts receive
an additional impulse of the same magnitude away from the posed pelvis center,
using the severed physics body's center to choose the outward direction. Heads
retain shot-direction-only launch. The two impulses add without renormalizing.
Connected sections follow
through their joints, so heavier detached assemblies tend to fly more slowly.
This prototype runtime choice introduces no weapon configuration settings.
The hit body also receives a random spin axis at 1–4 radians/second. Angular
velocity is added only to that body; connected detached sections respond through
their remaining physics joints.
Ragdoll colliders have rolling resistance so spheres and capsules settle after
ground contact. The severed hit body also has linear drag (1.5/s), limiting how
far light pieces travel on the existing local artificial floor. Launch impulse
and airborne spin are unchanged.

All humanoid ragdoll deaths also inherit a separate estimated linear velocity
per physics body from the preceding 1/30 second of animation. This is computed
once at death from the selected idle/walk/run/attack clip, in world meters/second,
capped at 20 m/s. It follows the displayed pose when available and adds to the
shot/blast launch; detached parts inherit it before their extra impulse/spin.
Every body also inherits the actual world-space walking/running velocity from
the last collision-constrained movement step, including terrain height changes.
This shared travel component is separate from the capped per-part clip motion;
stopped or blocked zombies do not retain their requested movement speed.

The prepared GLBs live locally under `models/severed/`, with content-addressed
filenames. Keep that directory with the original models and companion JSONs.
The existing asset packager includes the referenced meshes and definitions;
multiplayer validates their content fingerprints and replicates the selected
mesh with the complete corpse pose. These binaries retain the original asset's
licensing restrictions and are ignored by Git. They were prepared once locally;
there is no retained generator or automatic regeneration/test pipeline.
If a referenced mesh is missing or stale, restore the matching prepared set;
the viewer will report the affected companion file instead of using an
incompatible mesh. Custom assets without a companion file retain ordinary
body/head health hits.

Runtime checks (without generating assets):

```bash
./build/test_zombie_dismemberment assets/zombies/models/ZombieMale_A.glb
./build/test_zombie_melee_attack assets/zombies/models
./build/test_zombie_renderer --attacks assets/zombies/models/ZombieMale_A.glb build/attacks.ppm
./build/test_zombie_renderer --dismemberment assets/zombies/models/ZombieMale_A.glb build/dismemberment.ppm
```

## Reduced-detail meshes

Prepare three local mesh LODs from the existing runtime GLBs:

```bash
cmake -S . -B build
cmake --build build --target zombie_mesh_simplify -j4
.venv/bin/python tools/prepare_zombie_lods.py
```

CMake downloads the pinned, SHA-256 checked [meshoptimizer v1.3](https://github.com/zeux/meshoptimizer/tree/v1.3)
source archive into `build/_archives/` on first setup. The preparation command
uses local inputs and performs no downloads. LOD1 and LOD2 target 50% and 20%
of the original triangles; topology and error limits can stop reduction earlier.
The attribute metric considers normals, UV/color values and joint-space weights.
These levels retain the original vertex attributes. Existing LOD1/2 files are
reused when their source and output fingerprints match.

LOD3 uses the Slim 1 prism profile for all ten types: ten uncapped triangular
columns, 60 triangles and 60 shared vertices per character. It retains the
existing 80 m normal-FOV threshold and 5 Hz pose policy. Both torso rings are
15% narrower; only the hand/foot terminal rings shrink 30% about wrist/ankle
anchors. The upper limbs and proximal elbow/knee rings stay fixed. Lower
columns consequently taper toward their smaller tips. Source-derived linear
vertex colors replace base-color texture sampling; Male C keeps its verified
off-white vest, and the other types use their sampled authored colors.

All three levels preserve nodes, skins, inverse binds, scene transforms and
all animation clips. Original unused binary ranges are retained in the generated
GLBs to preserve animation/accessor bytes; runtime vertex/animation buffers
contain only the prepared meshes. Fitted LOD geometry is included in renderer
visibility bounds along with the original, including live-pose bind bounds.

Generated files live under `models/lod1/`, `models/lod2/`, and `models/lod3/`.
Content-addressed filenames keep the previously published set usable during an
interrupted rebuild. `models/lods.json` is replaced atomically after the entire
set succeeds; it records source/output hashes and mesh counts. These files stay
local and ignored, under the same asset licensing terms as the originals.
The viewer verifies hashes, bone order, hierarchy, binds and baked clip poses
before using LODs. Missing LODs use the original mesh; stale or incompatible
LODs produce a warning and fall back to the original. Rebuild LODs after rerunning
the asset importer. Additional mesh/animation buffers increase startup work and
retained memory in exchange for less GPU work per frame.

See [zombie rendering settings](../../docs/VIEWER.md#zombie-crowd) for distance,
animation and shadow policies and comparison flags.
