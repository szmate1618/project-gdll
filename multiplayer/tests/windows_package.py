"""Check the extracted Windows game from outside its package and checkout."""
import argparse
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def check_contents(package):
    required = [
        "godollo_client.exe", "OpenAL32.dll", "run-game.cmd", "README.md",
        "storage/equipment.toml", "storage/backpacks.toml", "storage/loadout.toml",
        "assets/test.glb", "assets/test.navmesh", "inventory.toml", "skills.toml", "skills/melee/punch.skill.toml",
        "ui/main-menu.rml", "ui/main-menu.rcss", "ui/main-menu-classic.rcss", "ui/main-menu-modern.rcss",
        "ui/fonts/menu-pixel.ttf",
        "ui/hud.rml", "ui/hud.rcss", "ui/command-wheel.rcss", "ui/palette.rcss", "ui/hud-icons.rcss", "ui/hud.vert", "ui/hud.frag", "licenses/RmlUi.txt",
        "gui/hud-icons.json", "data/assets/game-icons/hud/health.png", "data/assets/game-icons/hud/pistol.png",
        "data/assets/game-icons/license.txt", "credits/assets/HUD_ICONS.txt",
        "gui/character-faces.json", "credits/assets/CHARACTER_FACES.txt",
        "multiplayer/client/character_creation.vert", "multiplayer/client/character_creation.frag",
        "ui/inventory.rml", "ui/skills.rml", "ui/health.rml", "ui/map.rml", "ui/loadout.rml", "ui/loadout.rcss",
        "multiplayer/client/players.vert", "multiplayer/client/players.frag",
        "credits/assets/README.txt", "licenses/GameNetworkingSockets.txt",
    ]
    for name in required:
        if not (package / name).is_file():
            raise RuntimeError("Package is missing " + name)
    faces = json.loads((package / "gui/character-faces.json").read_text())["icons"]
    if len(faces) != 20:
        raise RuntimeError("Character creation requires the 20 selected face stamps")
    for name in faces:
        if not (package / "data/assets/game-icons/character-faces" / (name + ".png")).is_file():
            raise RuntimeError("Package is missing character face " + name)
    if not list((package / "vehicles").glob("*.toml")):
        raise RuntimeError("Package is missing vehicle definitions")
    for name in ("pistol", "submachine_gun", "shotgun", "sniper", "he_grenade"):
        for suffix in ("inventory.toml", "gameplay.toml", "tags.txt"):
            filename = f"storage/weapons/{name}.{suffix}"
            if not (package / filename).is_file():
                raise RuntimeError("Package is missing " + filename)
        model = package / "assets/weapons" / (name + ".glb")
        if not model.is_file() or model.read_bytes()[:4] != b"glTF":
            raise RuntimeError("Package is missing a generated weapon model: " + name)
    if not (package / "assets/audio/sources.json").is_file():
        raise RuntimeError("Package is missing audio resources")
    for name in ("pistol_rounds", "smg_rounds", "sniper_rounds", "shotgun_shells", "he_grenades"):
        for suffix in ("inventory.toml", "tags.txt"):
            filename = f"storage/ammo/{name}.{suffix}"
            if not (package / filename).is_file():
                raise RuntimeError("Package is missing " + filename)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("archive", type=Path)
    args = parser.parse_args()
    server = args.server.resolve()
    checks = Path(__file__).resolve().parent
    with tempfile.TemporaryDirectory(prefix="godollo Windows package ") as tmp:
        destination = Path(tmp)
        package = destination / "game files"
        with zipfile.ZipFile(args.archive) as archive:
            archive.extractall(package)
        check_contents(package)
        client = package / "godollo_client.exe"
        asset = package / "assets/test.glb"
        subprocess.run([client, "--help"], cwd=destination, check=True, timeout=15)
        # Includes private admission, embedded hosting, joining,
        # orderly shutdown and abrupt host loss using the actual packaged game.
        subprocess.run([sys.executable, checks / "game_modes.py", client, asset],
                       cwd=destination, check=True, timeout=45)
        # Check compatibility with the separately built dedicated server,
        # including content fingerprints, movement and packet loss.
        subprocess.run([sys.executable, checks / "loopback.py", server, client, asset],
                       cwd=destination, check=True, timeout=45)
        if os.name == "nt":
            # CALL handles a batch path containing spaces after cmd /c. The
            # scene is intentionally relative to the launcher, not this cwd.
            subprocess.run([os.environ.get("COMSPEC", "cmd.exe"), "/d", "/c", "call",
                            package / "run-game.cmd", "assets/test.glb", "--single-player",
                            "--headless", "--no-trees", "--zombie-count", "0", "--seconds", "1"],
                           cwd=destination, check=True, timeout=20)
        print("Relocated Windows game package and session checks passed")


if __name__ == "__main__":
    main()
