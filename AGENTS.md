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
| Setup, generator usage, geography | [README.md](README.md), `config.json`, `src/config.py` |
| Downloads and map publication | `src/data.py`, `src/generate.py`, `src/verify.py` |
| Terrain and town geometry | `src/terrain.py`, `src/osm.py`, `src/buildings.py`, `src/roads.py` |
| Tree assets and preprocessing | [docs/TREES.md](docs/TREES.md), [assets/README.md](assets/README.md), `tools/*tree*.py`, `src/tree_*.py` |
| Scene and tree loading | `src/model.hpp/.cpp`, `src/tree_layer.hpp/.cpp` |
| Animated zombie crowd | [assets/zombies/README.md](assets/zombies/README.md), `src/animated_model.hpp/.cpp`, `src/zombie_layer.hpp/.cpp`, `src/zombie_placement.hpp/.cpp`, `src/zombie_renderer.cpp` |
| Zombie ragdoll handoff and physics | `src/zombie_ragdoll_pose.hpp/.cpp`, `src/zombie_ragdoll.hpp/.cpp`, [docs/VIEWER.md](docs/VIEWER.md) |
| Rendering and visibility | `src/renderer.hpp/.cpp`, `src/visibility.hpp/.cpp`, `src/model_visibility.hpp/.cpp`, `src/tree_visibility.hpp/.cpp`, `src/gpu_timer.hpp/.cpp`, `shaders/` |
| Cameras, walking, collision | [docs/VIEWER.md](docs/VIEWER.md), `src/camera*`, `src/fps_controller*`, `src/collision_world*`, `src/collision_debug*` |
| App wiring and checks | `src/main.cpp`, `CMakeLists.txt`, corresponding `tests/test_*` files |

## Organize code for people

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
- Impostors use double-sided, unlit alpha masks. Keep opaque texel depth writes,
  discard transparent texels, and restore GL state between tree and town draws.
  Instance matrix attributes 4–7 belong to tree VAOs, with divisor 1.
- Canopy cards are rendering geometry. Walking receives separate trunk capsules
  with a spatial index independent of camera visibility. Ground queries exclude
  vegetation. Player positions denote feet; camera positions denote eyes.
- Preserve spatial acceleration for collision/visibility. Avoid a full forest
  scan per physics step or a draw call per tree. GPU timing stays asynchronous;
  presentation/VSync waits stay outside the render timers.

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
- Proceed with authorized, reversible work. Ask only for genuinely missing
  decisions or unapproved destructive/external actions, not routine code edits.
  Use the environment's permission mechanism when GPU/display access requires it.

## Verify the affected behavior

Run commands from the repository root. Start with focused checks and expand
when shared behavior changes; do not repeatedly rerun passing suites without a
new reason. Documentation-only changes need link/path and whitespace checks.

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
