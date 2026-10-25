# Windows game application

Asset and dataset credits are in `credits/assets/README.txt`; bundled software
licenses are in `licenses/`. For the town: map data © OpenStreetMap contributors,
ODbL 1.0, https://www.openstreetmap.org/copyright. Imagery and elevation use
Copernicus Sentinel-2 and Copernicus DEM GLO-30. The town asset pack adds
`credits/assets/MAP.txt` with the selected acquisition year and map fingerprint.
`credits/assets/VEGETATION.txt` separately identifies the selected forests and
trees, including their reference imagery. `credits/software/` lists runtime
and preparation software; client `licenses/` includes bundled JSON/stb and
Detour notices, together with platform dependency notices.
Keep these notices with any redistributed package.
Portions of this software are copyright © 2024 The FreeType Project
(www.freetype.org). All rights reserved.

This x64 game is built by the repository's **Build apps** GitHub workflow.
It requires a graphics driver supporting OpenGL 3.3 and the Microsoft Visual C++
2015–2022 x64 Redistributable:
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

Extract the complete ZIP and double-click **`run-game.cmd`**. The launcher sets
its working directory to the package folder and opens the game menu. It uses
`output/godollo.glb` when the town asset pack is installed; otherwise it selects
the included sample scene with trees and zombies disabled. Keep the resources
and `OpenAL32.dll` beside `godollo_client.exe`. The executable and artifact names
retain "client" for compatibility; this is the full game, including its server.

- **Single Player** starts a private, one-player loopback server automatically.
  It works without internet or an online service.
- **Host Game** starts the same authoritative server for up to eight total
  players, including the host. Set the UDP port in the menu (27020 by default).
  Other players join your numeric IP and port. Firewall permission and router
  port forwarding, where needed, are still manual.
- **Join Game** connects to the numeric IP and port in the menu without starting
  a local server. It can join integrated hosts or the standalone server.

Escape leaves a session and returns to the menu. Closing the window also stops
an embedded server; connection failures return to the menu for retry. No separate
server program needs to be installed or managed for Single Player or Host Game.

To start a mode directly, open PowerShell in the extracted folder:

```powershell
.\run-game.cmd assets/test.glb --single-player --no-trees --zombie-count 0
.\run-game.cmd assets/test.glb --host --port 27020 --no-trees --zombie-count 0
.\run-game.cmd assets/test.glb --connect SERVER_IP:27020 --no-trees --zombie-count 0
```

Arguments to the launcher are passed to `godollo_client.exe`; relative scene
paths are resolved from the package folder even when the launcher is invoked
from another working directory.

For the actual town, extract a matching asset ZIP created from the prepared
source checkout with `tools/package_multiplayer_assets.py` into a fresh game
folder. Preserve its `output/`, `assets/`, `weapons/`, and `items/` directories and matching
configuration files. The asset ZIP contains the map, companion layers, trees,
forest textures, and zombie models; it is shared with Linux and contains no
executables. The workflow does not download or generate those large assets.
After installing that content ZIP, use its **`play-town.cmd`** launcher to open
the town menu with the exact map and vegetation options selected by the packager.
This also supports packs made with custom map names or disabled layers. The
base `run-game.cmd` remains available for the sample scene or explicit arguments.
For joining, use the host's spawn, zombie-count/radius, and vegetation options.

Shaders (including the menu and player shaders), the five first-person weapon
models with attribution, weapon definitions, inventory, vehicles, and audio
are included. GLEW, OpenSSL, Protobuf and GameNetworkingSockets are linked into
the game; the Microsoft runtime is supplied by the Redistributable. The included
OpenAL DLL supplies audio (`--no-audio` to mute). Existing multiplayer gameplay
and audio limitations are documented in `multiplayer/README.md` in the source
repository.

The RmlUi HUD uses colored squares for artwork and the built-in Segoe UI font.
No HUD images or fonts are bundled. Set `GD_HUD_FONT` to an installed `.ttf`
path to select another system font.

The workflow builds the game, dedicated server and headless session checks. It
checks the cross-platform scene fingerprint, private single-player admission,
eight-player capacity, integrated hosting, joining, disconnects and dedicated
server compatibility. After packaging, it extracts the ZIP outside the checkout
and runs the packaged game's headless modes and launcher from another working
directory, including paths containing spaces. Interactive OpenGL rendering and
Linux-server/Windows-client gameplay still need Windows hardware validation.

## Dependency choices

`vcpkg.json` pins the package registry and Protobuf 3.21.12 to match the older
GameNetworkingSockets 1.4.1 build used on Linux. Both host and target use
`x64-windows-static-md`: static third-party libraries with the dynamic Microsoft
runtime. The build uses MSVC 2022 and CMake 3.31. The build copies its matching
OpenAL runtime DLL beside the game executable for both source-build startup and
packaging. Linux CI continues to run the full regression suites, including the
X11 game-menu test.
