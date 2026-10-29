# Linux game application

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

Download `godollo-client-linux-x86_64` from the **Build apps** workflow and
extract the enclosed tarball. It targets Ubuntu 24.04 x86-64 (glibc 2.39) and
compatible newer desktop Linux systems, with X11/XWayland and an OpenGL 3.3
graphics driver. Older glibc versions and 32-bit systems are not supported.
On Ubuntu, the desktop runtime packages can be installed with:

```sh
sudo apt install libopengl0 libgl1 libstdc++6 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6 fonts-dejavu-core
```

Start with the launcher so the included libraries are found:

```sh
./run-client.sh assets/test.glb --no-trees --zombie-count 0
```

Choose Single Player, Host Game or Join Game from the menu. Single Player and
Host Game include an embedded server; no separate server installation is needed.
Escape leaves a session and returns to the menu. Edit the UDP host port or numeric
join address in the menu. Direct startup is also available with `--single-player`,
`--host [--port 27020]`, or `--connect SERVER_IP:27020`. Hosting uses direct UDP;
firewall rules and router port forwarding remain manual. Remote peers must use
the same scene and options. The executable/launcher retain their existing client
names for compatibility. The package contains the game,
shaders, the five first-person weapon models and their attribution,
weapon/inventory configuration, forest configuration, a small sample
scene, and third-party library notices. It includes the client libraries found
at build time, except glibc, the C++ runtime, and the OpenGL dispatch/driver stack; those come from
the destination system. A desktop X11/XWayland installation is also required.
The launcher resolves relative map paths from the package folder.

Use `--fullscreen --resolution 960x540` for a desktop-resolution fullscreen
window with an upscaled 3D scene and sharp native-resolution HUD. Resolution
presets are `960x540`, `1280x720` and `1920x1080`; the flag also fixes the 3D
resolution in a resizable window. Without an override, windowed rendering follows
the window, while fullscreen renders at 1280x720. Display settings are client-only.

The HUD uses RmlUi with colored-square artwork placeholders. Its font comes
from the installed DejaVu, Noto or Liberation Sans system fonts; set
`GD_HUD_FONT` to another installed `.ttf` path to override that choice.

For the actual town, extract a matching `godollo-assets.zip` into this folder
after extracting the client into a fresh folder. The host can create it with
`tools/package_multiplayer_assets.py` from a prepared source checkout. Then run:

```sh
./run-client.sh output/godollo.glb
```

For a performance comparison without cars, add `--no-cars`. With Single Player,
`./play-town.sh --single-player --no-cars` disables them on the embedded server
and client; separate servers and clients must all use the same setting.

The content ZIP also supplies `play-town.sh`: use `./play-town.sh` (or
`sh play-town.sh`) to open the town menu with that pack's exact map, vegetation
and disabled-layer options. It accepts extra arguments such as `--single-player`
or `--host`. Use the server's spawn, zombie-count/radius, and vegetation options. The CI build
does not generate or bundle the large town, tree, or zombie assets. Gameplay
retains the multiplayer prototype's current limitations. Gameplay audio and
wind ambience use OpenAL; use `--no-audio` to mute. Audio needs matching
protocol-20 clients and servers.

To package a local Linux build, run `cmake -P multiplayer/linux/package.cmake`.
Set `-DBUILD_DIR=/path/to/build` before `-P` for another build directory.
The packager uses Debian/Ubuntu copyright files for bundled system libraries.
For a custom static Protobuf build, also provide `-DPROTOBUF_LICENSE=/path/to/LICENSE`.
