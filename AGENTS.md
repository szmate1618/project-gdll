# Working in this repository

This project has a Python geodata generator and a Linux C++17 / OpenGL 3.3
viewer. Keep generation, data preparation, rendering, and collision separate.
Use this guide as a navigation aid; read the relevant code for current behavior.

## Start small

- Check `git status --short` and the relevant diff before editing. Preserve the
  user's changes. When resuming interrupted work, inspect saved files and any
  running commands before repeating a generation or build step.
- Use `rg --files` and scoped `rg -n` searches. Read the relevant header/API,
  implementation, focused tests, and callers; avoid dumping entire directories.
- Choose the relevant entry below. Read its detailed documentation only as
  needed. Do not load large generated JSON/CSV files into conversation context;
  inspect their schema, counts, or a few records with a script.

| Work area | Start here |
| --- | --- |
| Suggested optimization backlog | [tasks/suggested-optimizations.md](tasks/suggested-optimizations.md), [multiplayer up to 100,000 zombies](tasks/multiplayer-100k-zombies.md) |
| Standard benchmarks and reports | [docs/BENCHMARKS.md](docs/BENCHMARKS.md), [CPU-load-dependent GPU timing on integrated hardware](docs/benchmarks/cpu-gpu-overlap-2026-10-22.txt), `tests/benchmark_*.cpp`, `src/frame_profile.hpp/.cpp`, `tools/summarize_render_benchmark.py`; rendering comparisons include the 100,000-zombie open-area case |
| Simulation spatial queries and candidate reuse | [docs/SIMULATION_QUERIES.md](docs/SIMULATION_QUERIES.md): callers, cadence, existing indexes, and requirements for batched neighbor candidates |
| Known correctness tasks | [tasks/review-follow-ups.md](tasks/review-follow-ups.md) |
| Setup, generator usage, geography | [README.md](README.md), `config.json`, `src/config.py` |
| Downloads and map publication | `src/data.py`, `src/generate.py`, `src/fence_scene_update.py`, `src/glb.py`, `src/verify.py` |
| Terrain and town geometry | `src/terrain.py`, `src/osm.py`, `src/buildings.py`, `src/building_details.py`, `src/wall_openings.py`, `src/facade_surface.py`, `src/building_caps.py`, `src/roads.py`, `src/road_scene_update.py`, `tools/update_road_offset.py`, `src/railways.py`, `src/fences.py`, `src/terrain_surfaces.py`, `src/terrain_profile.py` |
| Railway texture and existing-map update | [assets/railways/README.md](assets/railways/README.md), `src/railway_texture.py`, `src/railway_texture_apply.py`, `tools/apply_railway_texture.py` |
| Tree assets and preprocessing | [docs/TREES.md](docs/TREES.md), [assets/README.md](assets/README.md), `tools/*tree*.py`, `tools/apply_pine_clusters.py`, `src/tree_*.py`, `src/satellite_tree_*.py` |
| Runtime forest polygons, placement and LoD | [docs/FORESTS.md](docs/FORESTS.md), `forest_patches.json`, `forest_patches_astra.json`, `src/osm_vegetation*.py`, `tools/prepare_osm_vegetation.py`, `src/forest_review.py`, `src/forest_comparison*.py`, `tools/compare_forests.py`, `tools/build_astra_forests.py`, `src/forest_layer.hpp/.cpp`, `src/forest_geometry.hpp/.cpp`, `src/forest_placement.hpp/.cpp`, `src/forest_shrub.hpp/.cpp`, `src/forest_visibility.hpp/.cpp`, `src/forest_tree_visibility.hpp/.cpp`, `src/forest_volume_batch.hpp/.cpp`, `src/forest_renderer.cpp` |
| Scene and tree loading, canopy shading, card splitting, single-card LoD, depth ordering, blast response | `src/model.hpp/.cpp`, `src/model_merge.hpp/.cpp`, `src/tree_layer.hpp/.cpp`, `src/tree_canopy.hpp/.cpp`, `src/tree_card_split.hpp/.cpp`, `src/tree_billboard.hpp/.cpp`, `src/tree_depth_order.hpp/.cpp`, `src/vegetation_blast.hpp/.cpp`, `src/tree_renderer.cpp`, `src/file_fingerprint.hpp/.cpp` |
| Mapped utility columns and approximate HÉV masts | `tools/prepare_pole_instances.py`, `src/pole_placements.py`, `src/hev_pole_placements.py`, `src/pole_layer.hpp/.cpp`, [docs/VIEWER.md](docs/VIEWER.md#mapped-pole-columns) |
| Mapped roadside guardrails | `tools/prepare_guardrails.py`, `src/guardrails.py`, `src/guardrail_scene.py`, `src/guardrail_layer.hpp/.cpp`, [assets/guardrails/README.md](assets/guardrails/README.md), [docs/VIEWER.md](docs/VIEWER.md#mapped-guardrails) |
| Animated zombie crowd, perception, and shooting | [assets/zombies/README.md](assets/zombies/README.md), `src/animated_model.hpp/.cpp`, `src/zombie_animation.hpp/.cpp`, `src/zombie_animation_buffer.hpp/.cpp` (FP16 GPU storage), `src/zombie_lod.hpp/.cpp`, `src/zombie_lod_assets.cpp`, `src/zombie_lod.py`, `tools/prepare_zombie_lods.py`, `src/zombie_layer.hpp/.cpp`, `src/zombie_behavior.hpp/.cpp`, `src/zombie_attack.hpp/.cpp`, `src/zombie_lunge.hpp/.cpp`, `src/zombie_perception.hpp/.cpp`, `src/zombie_spatial_index.hpp/.cpp`, `src/zombie_crowd.hpp/.cpp`, `src/zombie_movement.hpp/.cpp`, `src/zombie_door_collision.hpp/.cpp`, `src/noise.hpp/.cpp`, `src/simulation_clock.hpp`, `src/zombie_hit.hpp/.cpp`, `src/zombie_hit_capsules.hpp/.cpp`, `src/zombie_damage.hpp/.cpp`, `src/zombie_dismemberment.hpp/.cpp`, `src/zombie_shooting.cpp`, `src/zombie_shot_query.cpp` (shared read-only traces and reported contacts), `src/zombie_chase.hpp/.cpp`, `src/zombie_placement.hpp/.cpp`, `src/zombie_park.hpp/.cpp`, `src/zombie_renderer.cpp`, `src/zombie_render_updates.hpp/.cpp`, `src/zombie_palette_cache.hpp/.cpp`, `src/zombie_bounds.hpp/.cpp` (shared conservative bounds), `src/zombie_visibility.hpp/.cpp` |
| Zombie navigation, scenarios and Detour steering | [runtime integration](docs/ZOMBIE_NAVIGATION_RUNTIME.md), [navmesh benchmarks](docs/ZOMBIE_PATHFINDING.md), `src/navmesh_preparation.py`, `src/map_publication.py`, `src/zombie_scenario*` (including scenario route following), `src/zombie_travel.hpp/.cpp` (distant segment refresh, full-movement fallback), `src/scenario_coordinates*`, `src/scenario_density.hpp/.cpp` (weighted endpoint images), `scenarios/test-horde/scenario.toml`, `src/zombie_navigation.hpp/.cpp`, `src/zombie_steering.hpp/.cpp`, `src/zombie_path_follow.hpp/.cpp`, `src/zombie_navmesh*`, `tools/bake_zombie_navmesh.py`, `tools/merge_zombie_navmesh.py`; map generation bakes navigation; scenario loaders share one mesh per standalone/server population and preplan once; original behaviors never request paths; clients never initialize it |
| Prototype weapons, inventory, ammo, recoil, scope and muzzle light | `storage/weapons/*.gameplay.toml`, [inventory descriptors and tags](storage/weapons/README.md), `src/inventory/item_config.hpp/.cpp`, `inventory.toml`, `src/weapons.hpp/.cpp`, `src/action_selection.hpp/.cpp` (hotbar preview/confirmation), `src/weapon_switch.hpp/.cpp` (draw/storage delay), `src/weapon_reload.hpp/.cpp` (ammo selection and reload timers), `src/weapon_overlay.hpp/.cpp`, `src/weapon_view_pose.hpp/.cpp`, `src/weapon_motion.hpp/.cpp`, `src/muzzle_flash.hpp/.cpp`, [weapon assets](assets/weapons/README.md), `tools/prepare_weapon_models.py`, `tools/build_weapon_assets.py`, [docs/VIEWER.md](docs/VIEWER.md#prototype-weapons-and-inventory) |
| Physical fists and melee contacts | `src/player_body.hpp/.cpp`, `src/player_body_renderer.hpp/.cpp`, `src/player_body_pose.hpp/.cpp`, `src/player_presentation.hpp/.cpp` (fixed-step camera/body interpolation), `src/melee_combat.hpp/.cpp`, `src/melee_contact_mesh.hpp/.cpp`, `src/zombie_bone_collision.hpp/.cpp`, `tools/benchmark_melee_collision.cpp`, [viewer controls](docs/VIEWER.md#prototype-weapons-and-inventory) |
| Thrown HE grenades, fuses and blasts | `src/grenades.hpp/.cpp`, `src/grenade_renderer.cpp`, `src/physics_scene.hpp/.cpp`, `src/zombie_layer.hpp/.cpp`, `storage/weapons/he_grenade.gameplay.toml`, `assets/weapons/he_grenade.json`, `tools/prepare_grenade_model.py` |
| Dropped equippable weapons | `src/dropped_weapons.hpp/.cpp`, `src/dropped_weapon_renderer.cpp`, `src/weapons.hpp/.cpp`, `src/box_layer.hpp/.cpp`, [docs/VIEWER.md](docs/VIEWER.md#discarding-and-picking-up-weapons) |
| Audio listener, effects, ambience, shrub noise and zombie voices | [docs/AUDIO.md](docs/AUDIO.md), [asset credits](assets/audio/CREDITS.md), `src/audio.hpp/.cpp`, `src/audio_wave.hpp/.cpp`, `src/scene_audio.hpp/.cpp`, `src/undergrowth.hpp/.cpp`, `src/undergrowth_audio.hpp/.cpp`, `src/zombie_audio.hpp/.cpp`, `src/door_audio.hpp/.cpp`, `multiplayer/client/audio_playback.hpp/.cpp`, `multiplayer/client/weapon_audio.hpp/.cpp`, `multiplayer/client/weapon_feedback.hpp/.cpp` (shared local shot effects), `multiplayer/client/remote_weapon_playback.hpp/.cpp` (event-timed remote guns), `multiplayer/client/sound_cues.hpp/.cpp`, `multiplayer/server/sounds.hpp/.cpp`, `multiplayer/sound_events.hpp`, `tools/create_audio_assets.py`, `tools/prepare_audio_assets.py` |
| Door interaction, hinges and shared rigid-body physics | `src/door_interaction.hpp/.cpp`, `src/physics_world.hpp/.cpp`, `src/door_geometry.hpp/.cpp`, `src/door_layer.hpp/.cpp`, `src/door_collision.hpp/.cpp`, [docs/VIEWER.md](docs/VIEWER.md) |
| Vehicle model downloads, licenses, simplification and comparisons | [assets/vehicles/README.md](assets/vehicles/README.md), `tools/download_vehicle_models.py`, `tools/prepare_vehicle_models.py`, `tools/vehicle_mesh_simplify.cpp`, `src/vehicle_assets.py`, `src/vehicle_asset_render.py`, `tools/prepare_vehicle_lods.py`, `src/vehicle_lod.py`, `src/vehicle_lod.hpp/.cpp`, [LoD preparation](assets/vehicles/README.md#vehicle-lods) |
| Prototype cars, suspension, driving and impacts | `src/vehicle_layer.hpp/.cpp`, `src/vehicle_physics.hpp/.cpp`, `src/vehicle_placement.hpp/.cpp`, `src/vehicle_geometry.cpp`, `src/vehicle_traction.hpp/.cpp`, `src/vehicle_steering.hpp/.cpp`, [Box3D extension](patches/README.md), `src/vehicle_player.hpp/.cpp`, `src/vehicle_zombie_collision.hpp/.cpp`, `src/vehicle_renderer.cpp`, `src/vehicle_visibility.cpp`, `multiplayer/server/vehicle_seats.hpp/.cpp`, `src/vehicle_config.hpp/.cpp`, [vehicle definitions](vehicles/README.md), [docs/VIEWER.md](docs/VIEWER.md#prototype-vehicles) |
| Runtime interior boxes, placement, grabbing and pushing | `src/interior_objects.hpp/.cpp`, `src/box_layer.hpp/.cpp`, `src/physics_prop.hpp/.cpp`, `src/physics_grab.hpp/.cpp`, `src/object_interaction.hpp/.cpp`, `src/player_interaction.hpp/.cpp`, `src/physics_scene.hpp/.cpp`, `src/box_renderer.cpp`, [docs/VIEWER.md](docs/VIEWER.md) |
| Streamed forest doodads and persistent moved props | [docs/DOODADS.md](docs/DOODADS.md), `src/doodad_placement.hpp/.cpp`, `src/doodad_state.hpp/.cpp`, `src/doodad_layer.hpp/.cpp`, `src/doodad_renderer.cpp`, [assets/doodads/README.md](assets/doodads/README.md), `cmake/Doodads.cmake` |
| Zombie ragdoll handoff, head/torso wounds and cavity contents | `src/zombie_ragdoll_pose.hpp/.cpp`, `src/zombie_ragdoll.hpp/.cpp`, `src/zombie_wounds.hpp/.cpp`, `src/zombie_wound_surface.hpp/.cpp`, `src/zombie_cavity_physics.hpp/.cpp`, `src/zombie_wound_renderer.cpp`, [docs/VIEWER.md](docs/VIEWER.md) |
| Bullet blood mist, pooled droplets and floor stains | `src/blood_mist.hpp/.cpp`, `src/blood_particles.hpp/.cpp`, `src/zombie_blood.cpp`, `src/blood_mist_renderer.hpp/.cpp`, `shaders/blood_mist.vert/.frag`, [docs/VIEWER.md](docs/VIEWER.md) |
| Rendering, lighting, and visibility | `src/renderer.hpp/.cpp`, `src/renderer_presentation.cpp` (scene upscaling, native-resolution GUI target, captures), `src/renderer_shadows.cpp`, `src/building_batch.hpp/.cpp`, `src/building_renderer.cpp`, `src/sun_lighting.hpp/.cpp`, `src/shadow_map.hpp/.cpp`, `src/flashlight.hpp/.cpp`, `src/atmosphere.hpp`, `src/night_lighting.hpp`, `src/rain_renderer.hpp/.cpp`, `src/shader_program.hpp/.cpp`, `src/basic_shader_uniforms.hpp/.cpp` (per-program uniform locations), `src/visibility.hpp/.cpp`, `src/model_visibility.hpp/.cpp`, `src/tree_visibility.hpp/.cpp`, `src/gpu_timer.hpp/.cpp`, `shaders/` |
| Bird flocks, fly-through attacks and silhouettes | [bird flock prototype](docs/VIEWER.md#bird-flocks), [fixed-point synchronization](docs/BOIDS.md), `src/boid/`, `multiplayer/server/birds.hpp/.cpp`, `shaders/bird.vert/.frag`, `tests/test_bird_flock.cpp`, `tests/test_boid_simulation.cpp` |
| Day/night clock and lighting transitions | `src/day_night.hpp/.cpp`, [docs/VIEWER.md](docs/VIEWER.md#daynight-cycle) |
| Player health, stamina and weariness | [viewer behavior](docs/VIEWER.md), `src/player_vitals.hpp`, `src/gui/hud.hpp/.cpp`, `multiplayer/server/world.hpp/.cpp`, `tests/test_player_vitals.cpp`, `multiplayer/tests/test_zombie_attacks.cpp` |
| Cameras, walking, stances, collision, and player noise | [docs/VIEWER.md](docs/VIEWER.md), `src/camera*`, `src/fps_controller*`, `src/player_stance.cpp`, `src/character_step.hpp/.cpp`, `src/player_noise.hpp/.cpp`, `src/collision_world*`, `src/collision_debug*` |
| App wiring and checks | `src/main.cpp`, `src/presentation_viewport.hpp` (fitted scene and pointer mapping), `src/vegetation_selection.hpp/.cpp`, `CMakeLists.txt`, corresponding `tests/test_*` files |
| Item definitions, player inventory and storage transactions | [code and query API](docs/INVENTORY.md), [storage configuration](storage/SCHEMA.md), `src/inventory/item.hpp/.cpp`, `src/inventory/item_config.hpp/.cpp`, `src/inventory/inventory_config.hpp/.cpp`, `src/inventory/player_inventory.hpp/.cpp`, `src/inventory/storage_config.hpp/.cpp`, `src/inventory/inventory_placement.hpp/.cpp`, `src/inventory/inventory_access.hpp/.cpp` (backpack obstruction/support), `src/inventory/inventory_drag.hpp/.cpp`, `src/inventory/inventory_action.hpp` (multiplayer drag operations), `storage/ammo/` (stack definitions), `src/weapon_config.hpp/.cpp`, `cmake/Inventory.cmake`, `tests/test_player_inventory.cpp`, `tests/test_inventory*.cpp`, `tests/test_item_config.cpp`, `tests/test_storage_config.cpp` |
| Shared RmlUi HUD, skills and minimap | [HUD](docs/VIEWER.md#hud), `ui/` (RmlUi documents/layout), `ui/palette.rcss` (shared HUD/loadout colors), `src/gui/hud.hpp/.cpp`, `src/gui/hud_render_interface.hpp/.cpp`, `src/command_wheel.hpp` (command sectors), `src/gui/command_wheel.hpp/.cpp` (shared HUD wheel and signal labels), `src/gui/inventory_view.hpp/.cpp`, `src/gui/inventory_interaction.hpp/.cpp`, `src/gui/hud_pointer.hpp` (local pointer events), `src/map_view.hpp/.cpp` (projection and measurement state), `src/gui/map_view.hpp/.cpp` (map controls and player markers), `src/gui/action_hotbar.hpp/.cpp`, `src/gui/skills_view.hpp/.cpp`, `skills.toml`, `skills/melee/*.skill.toml`, `src/skills/skills_config.hpp/.cpp`, `src/minimap.hpp/.cpp` (published terrain UV mapping), `src/minimap_roads.hpp/.cpp` (road preparation), `src/gui/minimap_road_renderer.hpp/.cpp` (cached road batches and BVH), `cmake/Hud.cmake`, `tests/test_hud.cpp`, `tests/test_map_view.cpp`, `tests/test_skills_config.cpp`, `tests/test_minimap*.cpp` |
| Optional multiplayer client/server and asset packaging | [multiplayer/README.md](multiplayer/README.md), `tools/package_multiplayer_assets.py`, `src/asset_pack_runtime.py` (navigation companions and pack launchers), `multiplayer/CMakeLists.txt`, `multiplayer/protocol.hpp/.cpp`, `multiplayer/scene.hpp/.cpp`, `multiplayer/asset_validation.hpp/.cpp`, `multiplayer/outgoing_snapshots.hpp/.cpp`, `multiplayer/network.hpp/.cpp`, [Windows UDP binding patch](patches/gamenetworkingsockets-README.md), `multiplayer/server/replication.hpp/.cpp`, `multiplayer/server/vehicle_replication.hpp/.cpp` (acknowledged car changes), `multiplayer/server/zombie_selection.hpp/.cpp` (cell-ordered interest), `multiplayer/client/snapshots.hpp/.cpp` (persistent ID-ordered playback), `multiplayer/client/vehicle_playback.hpp/.cpp` (sparse car histories), `multiplayer/client/zombie_prediction.hpp/.cpp` (living root and animation prediction), `multiplayer/client/ragdoll_playback.hpp/.cpp`, `multiplayer/client/ragdoll_interpolation.hpp/.cpp` (buffered joint-pose smoothing), `multiplayer/server/runtime.hpp/.cpp` (dedicated/embedded server lifecycle), `multiplayer/client/main.cpp` (game modes), `multiplayer/client/menu.hpp/.cpp`, `multiplayer/client/display.hpp`, `multiplayer/client/window.hpp/.cpp` (fullscreen and native HUD presentation), `multiplayer/client/session.hpp/.cpp`, `multiplayer/client/firearm_hits.hpp/.cpp` (trusted displayed-pose hit reports), `multiplayer/tests/` |
| Multiplayer client builds and packaging | [Linux package](multiplayer/linux/README.md), [Windows package](multiplayer/windows/README.md), `multiplayer/linux/package.cmake`, `multiplayer/tests/linux_package.py`, `multiplayer/windows/vcpkg.json`, `multiplayer/windows/run-game.cmd`, `multiplayer/tests/windows_package.py`, `.github/workflows/build-apps.yml`, `src/opengl.hpp`, `multiplayer/resources.hpp/.cpp` |

## Organize code for people

- Use "equippable" for equipment/inventory objects. Physics-hand props are
  "grabbable"; "pick up" describes the action of acquiring an equippable item.
- Prefer a focused new file/module for a new responsibility instead of growing
  an unrelated existing file. Keep CLI entry points and `main.cpp` centered on
  orchestration; put reusable behavior in named modules.
- **Extract or split existing code when doing so makes it more human-readable
  or maintainable.** Routine, task-related refactoring is encouraged. Keep moves
  behavior-preserving where possible and distinguish them from behavior changes.
- Name modules after their purpose. Use small, explicit interfaces and clear
  data ownership. Keep parsing, geometry calculations, spatial queries, GPU
  resources, and user interaction independently understandable and testable.
- Avoid catch-all `utils` files, speculative frameworks, and a separate file
  for every tiny function. Split along meaningful responsibilities, not an
  arbitrary line limit. Keep unrelated rewrites outside the current task.
- Update imports/includes, CMake targets, callers, tests, and documentation when
  moving code. Preserve existing public contracts unless changing them is part
  of the task; version incompatible data-format changes.
- Document units, coordinate frames, ownership, and reasons for non-obvious
  choices. Prefer clear names and short functions over comments that narrate code.
- For parallel work, agree on interfaces and non-overlapping file ownership
  first. Review the combined result and verify integration before reporting done.

## Preserve these contracts

### Geography and generated data

- `config.json` is the user's area/configuration. Derive the frame through
  `src/config.py`; do not hardcode a center, map size, or machine-specific path.
  Geographic bounding boxes are `[west, south, east, north]` in longitude/latitude.
- Generator geometry uses local east/north/up meters. The exported glTF/runtime
  frame is **X east, Y up, Z south**. Apply scene-node transforms once. Terrain
  heights are already relative to the recorded datum; do not subtract it again.
- Ground placement must match the rendered triangle surface. Tree preparation
  reads the published GLB through `src/tree_map.py`: cached terrain files can
  belong to a newer, failed generation. Preserve map/asset fingerprint checks.
- Detector boxes are image pixels, not meters or tree heights. Use the matching
  TIFF transform and usable CRS; a local-only CRS needs an explicit override.
  Preserve separate horizontal scales and provenance. See the tree docs for
  the existing EOV dataset; do not assume every future input uses that CRS.
- Keep building solids closed with consistent outward winding, including after
  float32 GLB export. Retain courtyard holes and boundary vertices. Validate
  geometry rather than disabling checks to make an export succeed.
- Preserve atomic publication: validate temporary output before replacing the
  last good map, merged CSV, or placement JSON.

### Viewer and trees

- Tree matrices are authoritative and column-major. Preserve rotation,
  reflection, and shear; rendering composes `instance * asset-node transform`.
- Share tree meshes/textures and batch instances. Cull using full transformed
  geometry bounds, including transparent padding. Visible canopy dimensions
  have a different purpose from conservative rendering bounds.
- Authored impostors use double-sided, unlit alpha masks; camera-facing distant
  cards are single-sided. Keep opaque texel depth writes,
  discard transparent texels, and restore GL state between tree and town draws.
  Instance matrix attributes 4–7 belong to tree VAOs, with divisor 1.
- Canopy cards are rendering geometry. Walking receives separate trunk capsules
  with a spatial index independent of camera visibility. Ground queries exclude
  vegetation. Player positions denote feet; camera positions denote eyes.
- Preserve spatial acceleration for collision/visibility. Avoid a full forest
  scan per physics step or a draw call per tree. GPU timing stays asynchronous;
  presentation/VSync waits stay outside the render timers.

### Multiplayer resource fingerprints

- Keep `storage/weapons/*.toml`, `storage/weapons/*.tags.txt`, `inventory.toml`,
  and `forest_patches.json` in **LF** form.
  The handshake hashes raw file bytes, so Windows CRLF conversion changes the
  fingerprint even when the parsed settings are identical, causing a content
  mismatch with Linux. Preserve their `.gitattributes` rules. When adding another
  tracked text resource to the handshake fingerprint, add a matching LF rule;
  do not bypass fingerprint validation to accommodate line-ending differences.

## Protect the workspace

- Treat ignored data as valuable. Do not delete caches, datasets, generated
  maps, or user assets as cleanup. Do not force-add generated binaries to Git;
  follow `.gitignore`. Source PNGs, prompts, docs, and configuration may be
  intentional repository inputs, so do not blanket-ignore non-code files.
- Reuse `.venv/`, pinned dependencies, and the out-of-source `build/` when
  available. A clean checkout may lack these and the generated map/tree GLBs;
  use the documented setup and asset builders when needed.
- Prefer cached/offline inputs. The default generator can download large data;
  CMake's first configure downloads dependencies. Do not regenerate the town
  merely to check a viewer or documentation change. Even `--offline` generation
  writes project outputs; changing `--config` does not isolate those outputs.
- Use small synthetic fixtures and scratch files under `build/` or `data/tmp/`
  for experiments. Do not resize the user's configured area to speed up tests.
- When restoring experimental C++ sources/headers, do not preserve old mtimes
  (`shutil.copy2`, `cp -p`, `rsync -t`): CMake's Make/Ninja builds may reuse newer
  objects from a different variant. Use `shutil.copyfile` or refresh every
  restored file's mtime, including headers. If stale objects may already exist,
  rebuild affected translation units and their dependents, or use a separate
  build directory; rerunning CMake configuration alone does not force this.
  Check the rebuilt variant before benchmarking: mixed class layouts can cause
  ABI corruption or stack-smashing failures, not just misleading timings.
- Historical control `.cpp` files must compile against the same headers as the
  linked libraries. Quoted includes search beside the source before `-I` paths:
  flatten control sources into a scratch directory without historical headers
  and inspect compiler dependencies. Rebuild every layout-dependent consumer.
- For viewer movement benchmarks, use `--fixed-step --frames N` so every run
  advances the same simulated time. Without `--fixed-step`, wall-clock frame
  deltas make faster variants perform different movement work and change the
  per-frame frequency of timed AI updates. `--animation-time` freezes only the
  render animation input, not `SimulationClock`. Match timestep policy across
  variants and record it; keep older wall-clock results labeled separately.
- Proceed with authorized, reversible work. Ask only for genuinely missing
  decisions or unapproved destructive/external actions, not routine code edits.
  Use the environment's permission mechanism when GPU/display access requires it.

## Verify the affected behavior

Run commands from the repository root. Start with focused checks and expand
when shared behavior changes; do not repeatedly rerun passing suites without a
new reason. Run tests only after a successful rebuild; otherwise old test
executables can report success for code that failed to compile. Documentation-only
changes need link/path and whitespace checks.

```bash
# Python: example focused suite; adjust the pattern to the changed module.
.venv/bin/python -m unittest discover -s tests -p 'test_tree_*.py' -v
# Full Python suite when warranted:
.venv/bin/python -m unittest discover -s tests -v

# Configure on initial setup or after changing build wiring, then build:
cmake -S . -B build
cmake --build build -j4
# Focused C++ example; use ctest -N to see available suites:
ctest --test-dir build -R '^viewer_tree_' --output-on-failure
# Full C++ suite when warranted:
ctest --test-dir build --output-on-failure

# Graphics check: needs an accessible display/GPU even with --hidden.
./build/godollo_viewer output/godollo.glb --hidden --frames 3 \
  --screenshot build/check.ppm
```

- Pure loader, camera, culling, and collision tests run without a GPU. The
  actual-map CTest integration case is registered when the map exists at CMake
  configuration time. Use [viewer docs](docs/VIEWER.md) for dependencies and controls.
- For rendering changes, inspect a capture from a useful camera position as well
  as GL errors. A successful draw or foreground-pixel count can just show a wall.
  `--no-trees`, `--no-tree-culling`, and `--no-tree-collisions` support comparisons.
  Software rendering is a diagnostic fallback, not proof of hardware validation.
- Add focused regressions for meaningful failures, especially coordinate/frame
  errors, stale manifests, export topology, GL state, and collision boundaries.
  Test outcomes independently rather than repeating implementation formulas.
- If a test fails, investigate the baseline and fixture/data assumptions. Do not
  weaken assertions or production validation just to obtain a green run.
- Finish with `git diff --check` and a scoped diff review. Report what changed,
  checks actually run, and any remaining limitations. For interrupted work,
  leave a short handoff: objective, changed paths, verification, and next step.

Keep this file compact and update its navigation when responsibilities move.
Put detailed designs and usage in the linked docs, not here. Avoid accumulating
session history, machine-specific paths, generated counts, or old test results.
