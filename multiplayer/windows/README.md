# Windows multiplayer client

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

For the actual town, copy the matching map and adjacent tree/pole/guardrail
layers from the server into `output/`, and its zombie models into
`assets/zombies/models/`. Tree and forest asset paths must also resolve to the
same content. The workflow does not download or generate those large assets.
Use the server's spawn, zombie-count/radius, and vegetation options:

```powershell
.\godollo_client.exe output/godollo.glb --connect SERVER_IP:27020
```

Shaders, player shaders, weapon definitions, inventory, and forest configuration
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
runtime. The build uses MSVC 2022 and CMake 3.31, and disables tests; Linux CI
continues to run the full regression suites.
