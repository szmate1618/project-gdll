#!/usr/bin/env python3
"""Download the pinned Game-icons.net pack and prepare the selected HUD PNGs."""
import hashlib
from io import BytesIO
import json
from pathlib import Path
import shutil
from urllib.request import urlopen
from zipfile import ZipFile

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def prepare(root=ROOT):
    manifest = json.loads((root / "gui/hud-icons.json").read_text())
    directory = root / "data/assets/game-icons"
    directory.mkdir(parents=True, exist_ok=True)
    archive = directory / "game-icons-white-transparent.zip"
    temporary = archive.with_suffix(".download")
    if not archive.exists():
        try:
            with urlopen(manifest["archive_url"], timeout=60) as source, temporary.open("wb") as output:
                shutil.copyfileobj(source, output)
            if hashlib.sha256(temporary.read_bytes()).hexdigest() != manifest["archive_sha256"]:
                raise ValueError("Game-icons.net archive differs from gui/hud-icons.json")
            temporary.replace(archive)
        finally:
            temporary.unlink(missing_ok=True)
    if hashlib.sha256(archive.read_bytes()).hexdigest() != manifest["archive_sha256"]:
        raise ValueError("Cached Game-icons.net archive differs from gui/hud-icons.json")

    destination = directory / "hud"
    destination.mkdir(exist_ok=True)
    size = manifest["texture_size"]
    with ZipFile(archive) as pack:
        (directory / "license.txt").write_bytes(pack.read("icons/license.txt"))
        for name, icon in manifest["icons"].items():
            source = "icons/ffffff/transparent/1x1/" + icon["source"]
            with Image.open(BytesIO(pack.read(source))) as image:
                image = image.convert("RGBA").resize((size, size), Image.Resampling.LANCZOS)
                temporary_image = destination / (name + ".tmp")
                image.save(temporary_image, format="PNG", optimize=True)
                temporary_image.replace(destination / (name + ".png"))
    print(f"Prepared {len(manifest['icons'])} HUD icons in {destination}")


if __name__ == "__main__":
    prepare()
