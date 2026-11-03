"""Combine a CI client package with verified scene content without changing inputs."""
from __future__ import annotations

import hashlib
import json
import os
from pathlib import Path, PurePosixPath, PureWindowsPath
import shutil
import stat
import tarfile
import tempfile
import zipfile

from src.github_client_builds import ARTIFACTS


def archive_name(name):
    path = PurePosixPath(name)
    if (not name or "\\" in name or path.is_absolute() or ".." in path.parts
            or PureWindowsPath(name).drive or ":" in name or "\0" in name):
        raise ValueError(f"Unsafe archive path: {name!r}")
    return path.as_posix()


def _zip_files(archive):
    files, seen = {}, set()
    for entry in archive.infolist():
        name = archive_name(entry.filename)
        if name.casefold() in seen:
            raise ValueError(f"Duplicate archive path: {name}")
        seen.add(name.casefold())
        kind = stat.S_IFMT(entry.external_attr >> 16)
        if kind not in (0, stat.S_IFREG, stat.S_IFDIR):
            raise ValueError(f"Unsupported ZIP entry: {name}")
        if not entry.is_dir():
            files[name] = entry
    return files


def unpack_client(artifact_path, platform, destination):
    """Unwrap the Actions ZIP and its nested game ZIP/tarball; retain Linux modes."""
    destination = Path(destination)
    destination.mkdir(parents=True)
    suffix = ".zip" if platform == "windows" else ".tar.gz"
    package_name = ARTIFACTS[platform] + suffix
    nested = destination / package_name
    with zipfile.ZipFile(artifact_path) as artifact:
        entries = _zip_files(artifact)
        if set(entries) != {package_name}:
            raise ValueError(f"Expected one {package_name} inside the GitHub artifact")
        with artifact.open(entries[package_name]) as source, nested.open("wb") as target:
            shutil.copyfileobj(source, target)
    unpacked = destination / "unpacked"
    unpacked.mkdir()
    if platform == "windows":
        with zipfile.ZipFile(nested) as archive:
            for name, entry in _zip_files(archive).items():
                target = unpacked / name
                target.parent.mkdir(parents=True, exist_ok=True)
                with archive.open(entry) as source, target.open("wb") as stream:
                    shutil.copyfileobj(source, stream)
    else:
        with tarfile.open(nested, "r:gz") as archive:
            # The data filter rejects escaping paths/links and device files.
            archive.extractall(unpacked, filter="data")
        unpacked /= ARTIFACTS[platform]
    executable = "godollo_client.exe" if platform == "windows" else "godollo_client"
    if not (unpacked / executable).is_file():
        raise ValueError(f"Client artifact is missing {executable}")
    return unpacked


def _configuration(name):
    return name in ("inventory.toml", "skills.toml", "doors.toml") or (
        name.startswith(("storage/", "skills/", "vehicles/"))
        and name.endswith((".toml", ".tags.txt")))


def check_local_configuration(client, root):
    """Fail before compressing large assets if this checkout differs from the build."""
    for path in sorted(Path(client).rglob("*")):
        name = path.relative_to(client).as_posix()
        if path.is_file() and _configuration(name):
            local = Path(root) / name
            if not local.is_file() or path.read_bytes() != local.read_bytes():
                raise ValueError(f"Client and local checkout differ at {name}; use the build's source revision or wait for a matching GitHub build")


def bundle_client(client, asset_pack, output, provenance):
    """Validate all inputs, stream one combined ZIP, then replace its destination."""
    client, output = Path(client), Path(output)
    base = {}
    for path in sorted(client.rglob("*")):
        if not path.resolve().is_relative_to(client.resolve()):
            raise ValueError(f"Client symlink escapes its package: {path}")
        if path.is_file():
            name = archive_name(path.relative_to(client).as_posix())
            base[name] = path
    if len({name.casefold() for name in base}) != len(base):
        raise ValueError("Client filenames collide on Windows")
    output.parent.mkdir(parents=True, exist_ok=True)
    with zipfile.ZipFile(asset_pack) as assets:
        entries = _zip_files(assets)
        if "client-build.json" in entries or "client-build.json" in base:
            raise ValueError("Input package already contains client-build.json")
        manifest = json.loads(assets.read("asset-pack.json"))
        if manifest.get("version") != 1 or not isinstance(manifest.get("files"), dict):
            raise ValueError("Unsupported asset-pack.json schema")
        hashes = manifest["files"]
        if set(entries) != {*hashes, "asset-pack.json"}:
            raise ValueError("Asset archive files do not match asset-pack.json")
        configs = {name for name in base if _configuration(name)}
        if configs != {name for name in hashes if _configuration(name)}:
            raise ValueError("Client and asset pack have different configuration file sets; use assets from the build's source revision")
        overlaps = base.keys() & hashes.keys()
        for name in sorted(overlaps):
            with base[name].open("rb") as source:
                digest = hashlib.file_digest(source, "sha256").hexdigest()
            if digest != hashes[name]:
                # Windows checkout converts notices to CRLF. These documents
                # are not handshake inputs; the asset copy retains its hash.
                if (name.startswith("credits/") and name.endswith((".txt", ".md"))
                        and base[name].read_bytes().replace(b"\r\n", b"\n")
                        == assets.read(entries[name]).replace(b"\r\n", b"\n")):
                    continue
                raise ValueError(f"Client and assets differ at {name}; use assets from the build's source revision")
        combined = {*base, *entries, "client-build.json"}
        if len({name.casefold() for name in combined}) != len(combined):
            raise ValueError("Client and asset filenames collide on Windows")
        with tempfile.NamedTemporaryFile(prefix=output.stem + "-", suffix=".zip", dir=output.parent, delete=False) as stream:
            temporary = Path(stream.name)
        try:
            prefix = ARTIFACTS[provenance["platform"]] + "/"
            with zipfile.ZipFile(temporary, "w", zipfile.ZIP_DEFLATED, compresslevel=6,
                                 strict_timestamps=False) as archive:
                for name, path in base.items():
                    if name not in entries:
                        # Dereference safe library symlinks: ZIP has no portable link semantics.
                        archive.write(path, prefix + name)
                for name, entry in entries.items():
                    info = zipfile.ZipInfo(prefix + name)
                    info.create_system = 3
                    info.external_attr = (0o100755 if name.endswith(".sh") else 0o100644) << 16
                    info.compress_type = zipfile.ZIP_DEFLATED
                    digest = hashlib.sha256()
                    with assets.open(entry) as source, archive.open(info, "w", force_zip64=True) as target:
                        while chunk := source.read(1024 * 1024):
                            target.write(chunk)
                            digest.update(chunk)
                    if name in hashes and digest.hexdigest() != hashes[name]:
                        raise ValueError(f"Asset checksum mismatch: {name}")
                archive.writestr(prefix + "client-build.json", json.dumps(provenance, indent=2) + "\n")
            with zipfile.ZipFile(temporary) as archive:
                if archive.testzip():
                    raise ValueError("Combined ZIP verification failed")
            os.replace(temporary, output)
        finally:
            temporary.unlink(missing_ok=True)
    return output
