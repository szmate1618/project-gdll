"""Collect published navigation companions and launch a prepared scene overlay."""
from __future__ import annotations

import shlex
import struct


def collect_navigation(assets, map_path):
    """Retain the published binary, replay and metadata as one matching set."""
    mesh = map_path.with_suffix(".navmesh")
    replay = map_path.with_suffix(".navigation.json")
    metadata = mesh.with_name(mesh.name + ".json")
    if not any(path.exists() for path in (mesh, replay, metadata)):
        return None  # Older/custom maps may have no authored scenario navigation.
    assets.add(mesh)
    with mesh.open("rb") as stream:
        header = stream.read(128)
    if len(header) != 128 or header[:8] not in (b"GDNAV01\0", b"GDNAV02\0"):
        raise ValueError(f"Invalid navigation header: {mesh}")
    vertices, polygons = struct.unpack_from("<II", header, 8)
    vertex_bytes, polygon_bytes = struct.unpack_from("<QQ", header, 80)
    if (not vertices or not polygons or vertex_bytes != vertices * 16
            or mesh.stat().st_size != 128 + vertex_bytes + polygon_bytes):
        raise ValueError(f"Navigation payload size mismatch: {mesh}")
    if metadata.exists() or replay.exists():
        document = assets.document(metadata)
        if document["map_sha256"] != assets.files[map_path.resolve()]:
            raise ValueError(f"Stale map fingerprint in {metadata}")
        assets.add(mesh, document["output_sha256"])
        if assets.reference(mesh.parent, document["source"]).resolve() != replay.resolve():
            raise ValueError(f"Unexpected navigation replay path in {metadata}")
        source = assets.document(replay)
        assets.add(replay, document["source_sha256"])
        if (header[96:].hex() != document["source_sha256"]
                or source["map_sha256"] != assets.files[map_path.resolve()]
                or document["format"] != header[:7].decode("ascii")):
            raise ValueError(f"Navigation provenance mismatch: {mesh}")
    return mesh.relative_to(assets.root).as_posix()


def pack_instructions(map_name, zombies, vegetation, trees, forests):
    arguments = [map_name, "--vegetation", vegetation]
    if not trees:
        arguments.append("--no-trees")
    elif not forests:
        arguments.append("--no-forests")
    arguments += ["--zombies", zombies] if zombies else ["--zombie-count", "0"]
    # Arguments are embedded in a Windows batch file as quoted literals.
    # Percent expansion is escaped; delayed expansion is explicitly disabled.
    if any(any(c in value for c in '\r\n"<>|?*') for value in arguments):
        raise ValueError("Pack launch paths must be valid portable Windows filenames")
    windows_arguments = " ".join('"' + value.replace("%", "%%") + '"' for value in arguments)
    windows = ('@echo off\nsetlocal DisableDelayedExpansion\ncd /d "%~dp0"\n'
               f'"%~dp0godollo_client.exe" {windows_arguments} %*\nexit /b %errorlevel%\n')
    linux = ('#!/bin/sh\nset -eu\n'
             'pack_dir=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)\n'
             f'exec "$pack_dir/run-client.sh" {shlex.join(arguments)} "$@"\n')
    readme = f'''GODOLLO GAME CONTENT PACK

1. Download the matching application artifact from GitHub Actions "Build apps".
   Windows: extract godollo-client-windows-x86_64 and its enclosed game ZIP.
   Linux: extract godollo-client-linux-x86_64 and its enclosed tarball.
2. Extract this asset ZIP INTO that game folder, beside godollo_client.exe or
   godollo_client, preserving all subfolders. Do not extract it into assets/.
3. Windows: double-click play-town.cmd. Linux: run ./play-town.sh (or sh play-town.sh).
4. Choose Single Player, Host Game or Join Game. Escape returns to the menu.

Map: {map_name}
Vegetation: {vegetation if trees else "none"}
The launchers apply this pack's map, vegetation, forest and zombie options.
Extra arguments are accepted, e.g. --single-player, --host or --connect IP:PORT.
All peers must use matching game builds, content and scene settings.

This ZIP contains prepared map geometry, embedded map metadata/textures,
published companion layers/navigation, scene models and definitions.
Scenario TOMLs are included for the standalone server/prototype viewer;
the integrated menu starts the normal game without a scenario.
asset-pack.json lists the content, hashes and launcher arguments. Navigation
is included when it exists beside the map. Raw datasets, preparation caches,
backups, executables and platform libraries are not part of this ZIP.
The application artifact supplies executables, libraries, shaders, weapon models,
audio and run-client.sh. Those build outputs are not duplicated in this ZIP.
No map generation or asset downloads occur during packaging or startup.

Keep credits/assets and credits/software with the package.
'''
    launch = {"windows": "play-town.cmd", "linux": "play-town.sh", "arguments": arguments}
    return launch, {"play-town.cmd": windows.replace("\n", "\r\n").encode("utf-8"),
                    "play-town.sh": linux.encode("utf-8"), "ASSET-PACK-README.txt": readme.encode("utf-8")}
