# Linux multiplayer client

Asset and dataset credits are in `credits/assets/README.txt`; bundled software
licenses are in `licenses/`. For the town: map data © OpenStreetMap contributors,
ODbL 1.0, https://www.openstreetmap.org/copyright. Imagery and elevation use
Copernicus Sentinel-2 and Copernicus DEM GLO-30. The town asset pack adds
`credits/assets/MAP.txt` with the selected acquisition year and map fingerprint.
`credits/assets/VEGETATION.txt` separately identifies the selected forests and
trees, including their reference imagery. `credits/software/` lists runtime
and preparation software; client `licenses/` includes bundled JSON/stb notices.
Keep these notices with any redistributed package.

Download `godollo-client-linux-x86_64` from the **Build apps** workflow and
extract the enclosed tarball. It targets Ubuntu 24.04 x86-64 (glibc 2.39) and
compatible newer desktop Linux systems, with X11/XWayland and an OpenGL 3.3
graphics driver. Older glibc versions and 32-bit systems are not supported.
On Ubuntu, the desktop runtime packages can be installed with:

```sh
sudo apt install libopengl0 libgl1 libstdc++6 libx11-6 libxrandr2 libxinerama1 libxcursor1 libxi6
```

Start with the launcher so the included libraries are found:

```sh
./run-client.sh assets/test.glb --connect SERVER_IP:27020 --no-trees --zombie-count 0
```

The server must use the same scene and options. The package contains the client,
shaders, the five first-person weapon models and their attribution,
weapon/inventory configuration, forest configuration, a small sample
scene, and third-party library notices. It includes the client libraries found
at build time, except glibc, the C++ runtime, and the OpenGL dispatch/driver stack; those come from
the destination system. A desktop X11/XWayland installation is also required.
The launcher resolves relative map paths from the package folder.

For the actual town, extract a matching `godollo-assets.zip` into this folder
after extracting the client into a fresh folder. The host can create it with
`tools/package_multiplayer_assets.py` from a prepared source checkout. Then run:

```sh
./run-client.sh output/godollo.glb --connect SERVER_IP:27020
```

Use the server's spawn, zombie-count/radius, and vegetation options. The CI build
does not generate or bundle the large town, tree, or zombie assets. Gameplay
retains the multiplayer prototype's current limitations. Local shrub rustles
have audio; use `--no-audio` to mute. Remote shrub sounds and other game audio
are not yet routed in multiplayer.

To package a local Linux build, run `cmake -P multiplayer/linux/package.cmake`.
Set `-DBUILD_DIR=/path/to/build` before `-P` for another build directory.
The packager uses Debian/Ubuntu copyright files for bundled system libraries.
For a custom static Protobuf build, also provide `-DPROTOBUF_LICENSE=/path/to/LICENSE`.
