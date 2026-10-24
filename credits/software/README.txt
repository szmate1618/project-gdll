SOFTWARE USED BY THE VIEWER, SERVER, CLIENT AND ASSET PREPARATION

The town asset ZIP contains generated data/models and notices, not the Python
interpreter or these libraries' binaries. The separately distributed client
includes compiled library code; its licenses/ folder carries the bundled
software's full license texts and platform-specific dependency notices.

Runtime dependencies (versions are pinned unless described as platform-selected):

GameNetworkingSockets 1.4.1 — Valve and contributors; BSD-3-Clause.
  https://github.com/ValveSoftware/GameNetworkingSockets
Protobuf — Google and contributors; BSD-3-Clause (see bundled platform notice).
  https://github.com/protocolbuffers/protobuf
GLFW 3.4 — Marcus Geelnard, Camilla Löwy and contributors; zlib/libpng license.
  https://www.glfw.org/license.html
GLM 1.0.1 — G-Truc Creation and contributors; see GLM.txt (dual license).
  https://github.com/g-truc/glm/blob/1.0.1/copying.txt
Box3D — Erin Catto; MIT, pinned commit f555ee42084e0b43cbffa863f40bff8117c08896.
  https://github.com/erincatto/box3d
  Includes a local directional tire-friction extension; see
  patches/box3d-directional-friction.patch and patches/box3d-LICENSE.txt.
TinyGLTF 2.9.7 — Syoyo Fujita, Aurélien Chatelain and contributors; MIT.
  https://github.com/syoyo/tinygltf/tree/v2.9.7
nlohmann JSON 3.10.4 — Niels Lohmann and contributors; MIT.
  Included through tinygltf/json.hpp; full bundled header notice is preserved
  in nlohmann-json.txt, separately from TinyGLTF's own license.
stb_image / stb_image_write — Sean Barrett and contributors; MIT or Unlicense.
  Full dual-license text from the pinned headers is preserved in stb.txt.
  https://github.com/nothings/stb
toml++ 3.4.0 — Mark Gillard; MIT.
  https://github.com/marzer/tomlplusplus/tree/v3.4.0
Recast Navigation / Detour 1.6.0 — Mikko Mononen and contributors; zlib license.
  Pinned commit 6dc1667f580357e8a2154c28b7867bea7e8ad3a7. The unmodified
  DetourObstacleAvoidance sampler and supporting math/allocation/assert sources
  are compiled for local zombie tree avoidance. Global route search uses this
  project's own navmesh code, not Recast's navmesh builder or DetourCrowd.
  Full upstream notice is preserved in recastnavigation.txt and in
  third_party/recastnavigation/License.txt.
  https://github.com/recastnavigation/recastnavigation/tree/6dc1667f580357e8a2154c28b7867bea7e8ad3a7
OpenSSL — OpenSSL Project and contributors; see platform's OpenSSL notice.
  https://www.openssl.org/source/license.html
GLEW — Windows OpenGL loader; see vcpkg's bundled copyright/license notice.
  https://glew.sourceforge.net/credits.html

The Linux package copies the copyright notices of the shared libraries it
actually bundles, including applicable compression/crypto dependencies.
Windows copies vcpkg copyright notices. The operating-system graphics driver,
C/C++ runtime and system OpenGL implementation are not supplied by the Linux
client archive; their presence on the host is not a license grant from us.

Meshoptimizer 1.3 — Arseny Kapoulkine and contributors; MIT. Used by the offline
zombie LoD builder, not linked into the multiplayer client. Full license is
preserved in meshoptimizer.txt.
  https://github.com/zeux/meshoptimizer/tree/v1.3

OpenAL Soft 1.24.3 (LGPL-2.0-or-later and accompanying third-party notices) is
used by the standalone viewer and graphical multiplayer client.
Distributing these binaries needs the separate
OpenAL and bundled third-party notices and applicable source/relinking terms.
  https://github.com/kcat/openal-soft/tree/1.24.3

PREPARATION_TOOLS.txt identifies the Python/geospatial toolchain used to make
these assets. These tools' code is not embedded in exported geometry, texture
pixels or placement JSON. The generated assets retain their separate data and
artwork provenance; naming a tool does not relicense provider data.
