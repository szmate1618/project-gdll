# Linux GLB viewer

`godollo_viewer` is a small C++17 desktop viewer for glTF 2.0 scenes. It opens a
resizable 1280 × 720 GLFW window, requests an OpenGL 3.3 Core context, and draws
the scene with a perspective free-fly or first-person walking camera and directional lighting. Startup
prints the OpenGL version, renderer, vendor, and GLSL version. The Python scene
generator remains a separate program.

## Linux dependencies

Install a C++17 compiler, CMake 3.20 or newer, Python 3, OpenGL development
headers/libraries, and X11 development packages. A GPU driver supporting OpenGL
3.3 and a working desktop display are required for normal interactive use.
Python is used only to generate the tiny test asset; no Python packages or
generator dependencies are needed to build the viewer.

Ubuntu / Debian:

```bash
sudo apt install build-essential cmake python3 pkg-config libgl1-mesa-dev \
  libx11-dev libxrandr-dev libxinerama-dev libxcursor-dev libxi-dev
```

Fedora:

```bash
sudo dnf install gcc-c++ cmake python3 pkgconf-pkg-config mesa-libGL-devel \
  libX11-devel libXrandr-devel libXinerama-devel libXcursor-devel libXi-devel
```

CMake downloads pinned versions of [GLFW 3.4](https://github.com/glfw/glfw/releases/tag/3.4),
[GLM 1.0.1](https://github.com/g-truc/glm/releases/tag/1.0.1), and
[tinygltf 2.9.7](https://github.com/syoyo/tinygltf/releases/tag/v2.9.7), verifying
each archive's SHA-256. The first configure needs network access. Downloads live
under `build/_archives/`; extracted dependencies and their build products live
under `build/_deps/`. Their examples, tests, and install targets are disabled.
Subsequent builds reuse these files. No global C++ dependency installation is
performed.

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

## Camera and coordinates

| Input | Action |
| --- | --- |
| W / S | Move forward / backward |
| A / D | Strafe left / right |
| Q / E | Move down / up along world Y in free-fly mode |
| Mouse | Look around while captured |
| Shift | Free-fly: move four times faster; walking: run at 6 m/s |
| Mouse wheel | Adjust free-fly movement speed |
| F1 | Switch to free-fly mode |
| F2 | Switch to walking mode; find safe ground below/near the camera |
| Space | Jump while grounded in walking mode |
| F3 | Toggle collision visualization |
| Tab | Release / capture the mouse |
| F | Frame the whole scene again in free-fly mode |
| Escape | Close the viewer |

The mouse is captured when the window opens. Movement uses elapsed frame time
and normalizes diagonal input. Mouse pitch is
limited to avoid flipping the camera. The camera initially frames the imported
scene's transformed bounds, including scenes away from the origin, and chooses
a movement speed based on scene size.

The viewer follows glTF's right-handed coordinate system: Y is up, and the
camera looks toward negative Z before orientation is applied. Distances are
interpreted as meters. The vertical field of view is 60°, near clipping is
0.1 m, and far clipping is at least 10,000 m, expanded when framing larger
scenes. The generator's existing root transform maps its east/north/up geometry
to glTF X=east, Y=up, Z=south; no scene-specific rotation is needed in the viewer.
Framebuffer resize updates the viewport and projection aspect ratio.

## First-person walking

Launch the generated town, then press **F2**, or start directly in walking mode:

```bash
./build/godollo_viewer output/godollo.glb --fps
./build/godollo_viewer output/godollo.glb --spawn 0 0
./build/godollo_viewer output/godollo.glb --fps --collision-debug
```

`--spawn X Z` requests a location in the loaded scene's local meter coordinates;
Y is found from the terrain. Entering walking mode searches for clear, walkable
terrain under or near the requested position, including when the free-fly
camera is above a building. The viewer logs mode changes. **F1** releases the
camera from the controller at its current eye position; gravity applies only
in walking mode.

The player is a vertical capsule, 1.80 m tall and 0.30 m in radius. The camera
sits 1.70 m above its feet. W/S/A/D movement uses yaw, independently of look
pitch, so looking up or down cannot make the player fly. Diagonal input is
normalized. Walking speed is 3 m/s; Shift increases it to 6 m/s. Gravity is
9.81 m/s² and a grounded Space press launches at 3.8 m/s, producing an apex
roughly 0.74 m above standing height on flat ground. Holding Space does not
repeat jumps, and presses in the air do not add another jump.

Collision data is built from the existing loaded GLB, with scene-node transforms
applied in the same Y-up coordinates used for rendering. The loader preserves
node/mesh names, identifying the generator's terrain, buildings, and roads.
A static triangle BVH limits collision queries to nearby geometry. Exact
capsule-to-triangle contacts follow building footprints, including concave
outlines, rather than replacing buildings with broad rectangular obstacles.
Named buildings also have an interior query for safe spawning. Roads and
other triangle geometry can provide support; a separate collision file is not
required. Generated buildings remain solid, with no inferred doors or interiors.

The controller uses fixed 120 Hz simulation and further divides large capsule
displacements. Iterative contact corrections stop motion into walls while
preserving movement along them. Ground normals guide walking on slopes up to
45°; steeper uphill contacts block climbing. A short ground snap keeps downhill
walking in contact with terrain. Leaving a larger drop enables falling, and
landing cancels downward velocity. Rounded contacts and a 0.30 m support snap
help with small mesh height changes; there is no dedicated stair-climbing solver,
so sharp vertical curbs can still block movement.

Important dimensions, speeds, gravity, jump strength, slope limit, contact skin,
ground snap, and simulation timestep live together in `FPSConfig` in
`src/fps_controller.hpp`. Change these defaults and rebuild to tune the controller.
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
The controller is a lightweight walking controller with no crouching, swimming,
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
`shaders/basic.frag` provides simple directional Lambert lighting. These GLSL
330 files are read at startup, so edit them and restart the viewer to experiment.
Use `--shader-dir /path/to/shaders` to load an alternate shader pair.
Shader errors print the source filename and compiler output. Model errors
include the input path. Debug builds print OpenGL debug messages when the
driver exposes the debug extension.

This is a basic geometry viewer, not a complete glTF rendering engine. It does
not provide animation, skeletal skinning, morph targets, shadows, or full PBR
lighting. Normal/metallic/roughness maps and texture coordinate sets beyond
`TEXCOORD_0` are not rendered. Transparent primitives are sorted as whole
objects, so intersecting transparent surfaces can show sorting artifacts.
Non-triangle primitives are skipped with a warning. Unsupported required glTF
extensions (including Draco/meshopt compression and KTX2 textures) are reported
as errors. Large
scenes are loaded into memory as a whole; there is no streaming or level of
detail system. Very large absolute coordinates remain subject to floating
point precision limits.

## Verification and diagnostics

The window title updates about once per second with
`FPS | GPU <average> ms | CPU render <average> ms`. FPS uses the actual frame
interval, including any VSync wait. GPU time uses asynchronous
`GL_TIME_ELAPSED` queries for the scene and enabled collision overlay; CPU
render time measures time spent submitting those draws, including driver
overhead and stalls. Both render timings exclude events, physics updates,
framebuffer resize, the presentation blit, and the buffer swap/VSync wait.
GPU results can lag and show `pending` or `unavailable` as appropriate. Compare
CPU and GPU timings separately: their work overlaps, so the times cannot be
added. Timing appears automatically in the title without additional console
output or command-line options.

Run the camera, loader, collision, and walking-controller checks without a graphical display:

```bash
ctest --test-dir build --output-on-failure
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

Exercise walking mode, mode switches, jump input, and the collision overlay with
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
src/renderer.{hpp,cpp}      OpenGL resources, shaders, drawing
src/camera.{hpp,cpp}        Perspective free-fly camera
src/camera_rig.{hpp,cpp}    Runtime camera mode switching
src/fps_controller.{hpp,cpp} Fixed-step walking, gravity, jump and contact response
src/collision_world.{hpp,cpp} World-space triangle BVH and capsule/ground queries
src/collision_debug.{hpp,cpp} Optional OpenGL collision line overlay
src/model.{hpp,cpp}         glTF loading and scene data
shaders/basic.{vert,frag}   GLSL 330 directional lighting
shaders/debug.{vert,frag}   GLSL 330 collision lines
tools/create_test_asset.py Reproducible test-model generator
assets/test.glb            Generated test model, ignored by Git
tests/test_camera.cpp      Camera checks
tests/test_camera_rig.cpp  Free-fly/walking mode switching and look-pitch checks
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
