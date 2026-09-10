# Linux GLB viewer

`godollo_viewer` is a small C++17 desktop viewer for glTF 2.0 scenes. It opens a
resizable 1280 × 720 GLFW window, requests an OpenGL 3.3 Core context, and draws
the scene with a perspective free-fly camera and directional lighting. Startup
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
| Q / E | Move down / up along world Y |
| Mouse | Look around while captured |
| Shift | Move four times faster |
| Mouse wheel | Adjust movement speed |
| Tab | Release / capture the mouse |
| F | Frame the whole scene again |
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

Run the camera and loader checks without a graphical display:

```bash
ctest --test-dir build --output-on-failure
```

For a finite render run that also exercises the input callbacks:

```bash
./build/godollo_viewer --frames 3 --self-test-input --screenshot build/test.ppm
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

Verified in this workspace: CMake configure and build; both CTest suites;
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
src/model.{hpp,cpp}         glTF loading and scene data
shaders/basic.{vert,frag}   GLSL 330 directional lighting
tools/create_test_asset.py Reproducible test-model generator
assets/test.glb            Generated test model, ignored by Git
tests/test_camera.cpp      Camera checks
tests/test_model.cpp       Loader checks
build/                    Generated files, dependencies, and executable
```

The existing Python source, tests, `data/`, and `output/` continue to serve the
scene generator. Generated GLB assets and build output remain ignored by Git.

Dependency license notices remain in the downloaded source trees: GLFW's
`LICENSE.md` (zlib/libpng), GLM's `copying.txt` (MIT or Happy Bunny), and
tinygltf's `LICENSE` (MIT). tinygltf bundles nlohmann/json (MIT) and stb_image
(MIT or public domain), with their notices in the corresponding headers.
Preserve the applicable notices when redistributing dependency code or binaries.
