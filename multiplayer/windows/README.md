# Windows multiplayer client

Asset and dataset credits are in `credits/assets/README.txt`; bundled software
licenses are in `licenses/`. For the town: map data © OpenStreetMap contributors,
ODbL 1.0, https://www.openstreetmap.org/copyright. Imagery and elevation use
Copernicus Sentinel-2 and Copernicus DEM GLO-30. The town asset pack adds
`credits/assets/MAP.txt` with the selected acquisition year and map fingerprint.
Keep these notices with any redistributed package.

This x64 client is built by the repository's **Build apps** GitHub workflow.
It requires a graphics driver supporting OpenGL 3.3 and the Microsoft Visual C++
2015–2022 x64 Redistributable:
https://learn.microsoft.com/en-us/cpp/windows/latest-supported-vc-redist

Extract the complete ZIP, keep the resource folders beside `godollo_client.exe`,
and open PowerShell in that folder. The sample scene is included for a quick
connection check against a server using the same scene:

```powershell
.\godollo_client.exe assets/test.glb --connect SERVER_IP:27020 --no-trees --zombie-count 0
```

For the actual town, ask the host for an asset ZIP created from the prepared
source checkout with `tools/package_multiplayer_assets.py`. Extract it into the
same fresh folder as the client ZIP, preserving its `output/`, `assets/`, and
`items/` directories and matching configuration files. The asset ZIP contains
the map, companion layers, trees, forest textures, and zombie models; it is
shared with Linux and contains no executables. The workflow does not download
or generate those large assets. Use the server's spawn, zombie-count/radius,
and vegetation options:

```powershell
.\godollo_client.exe output/godollo.glb --connect SERVER_IP:27020
```

Shaders, player shaders, the four first-person weapon models with attribution,
weapon definitions, inventory, and forest configuration
are included. GLEW, OpenSSL, Protobuf and GameNetworkingSockets are linked into
the client; the Microsoft runtime is supplied by the Redistributable.
The client still has the prototype limitations described in `multiplayer/README.md`
in the source repository, including no audio playback.

The workflow compiles the client, checks `--help` startup, and runs the
cross-platform scene fingerprint regression. Windows graphics and cross-platform
gameplay need a real Windows machine for validation.

## Dependency choices

`vcpkg.json` pins the package registry and Protobuf 3.21.12 to match the older
GameNetworkingSockets 1.4.1 build used on Linux. Both host and target use
`x64-windows-static-md`: static third-party libraries with the dynamic Microsoft
runtime. The build uses MSVC 2022 and CMake 3.31, and runs the focused fingerprint
test; Linux CI continues to run the full regression suites.
