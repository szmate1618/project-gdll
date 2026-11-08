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
        "assets/test.glb", "assets/test.navmesh", "inventory.toml", "doors.toml", "skills.toml", "skills/melee/punch.skill.toml",
        "assets/pigeon/flock.json", "credits/assets/PIGEONS.txt", "credits/assets/UNDEAD_DOGS.txt",
        "ui/main-menu.rml", "ui/main-menu.rcss", "ui/main-menu-classic.rcss", "ui/main-menu-modern.rcss",
        "ui/fonts/menu-pixel.ttf", "ui/character-editor.rcss", "ui/host-game.rcss",
        "ui/fonts/plex/IBMPlexSans-Regular.ttf", "ui/fonts/plex/IBMPlexSansCondensed-Medium.ttf",
        "ui/fonts/plex/IBMPlexMono-Regular.ttf", "licenses/IBM-Plex.txt",
        "credits/assets/FONTS.txt", "credits/assets/IBM_PLEX_OFL.txt",
        "ui/hud.rml", "ui/hud.rcss", "ui/command-wheel.rcss", "ui/palette.rcss", "ui/hud-icons.rcss", "ui/hud.vert", "ui/hud.frag", "licenses/RmlUi.txt",
        "ui/hud-backdrop.vert", "ui/hud-backdrop.frag", "ui/hud-backdrop-update.vert", "ui/hud-backdrop-update.frag",
        "gui/hud-icons.json", "data/assets/game-icons/hud/health.png", "data/assets/game-icons/hud/pistol.png",
        "data/assets/game-icons/license.txt", "credits/assets/HUD_ICONS.txt",
        "gui/character-faces.json", "credits/assets/CHARACTER_FACES.txt",
        "multiplayer/client/character_creation.vert", "multiplayer/client/character_creation.frag",
        "ui/inventory.rml", "ui/skills.rml", "ui/skills.rcss", "ui/health.rml", "ui/map.rml", "ui/settings.rml", "ui/settings.rcss", "ui/loadout.rml", "ui/loadout.rcss",
        "multiplayer/client/players.vert", "multiplayer/client/players.frag",
        "credits/assets/README.txt", "licenses/GameNetworkingSockets.txt",
    ]
    for name in required:
        if not (package / name).is_file():
            raise RuntimeError("Package is missing " + name)
    license = (package / "credits/assets/IBM_PLEX_OFL.txt").read_bytes()
    if b"SIL OPEN FONT LICENSE Version 1.1" not in license or license != (package / "licenses/IBM-Plex.txt").read_bytes():
        raise RuntimeError("Packaged IBM Plex license must match the original notice")
    if "IBM Plex" not in (package / "credits/assets/FONTS.txt").read_text(encoding="utf-8"):
        raise RuntimeError("Package is missing IBM Plex attribution")
    faces = json.loads((package / "gui/character-faces.json").read_text())["icons"]
    if len(faces) != 20:
        raise RuntimeError("Character creation requires the 20 selected face stamps")
    for name in faces:
        if not (package / "data/assets/game-icons/character-faces" / (name + ".png")).is_file():
            raise RuntimeError("Package is missing character face " + name)
    if not list((package / "vehicles").glob("*.toml")):
        raise RuntimeError("Package is missing vehicle definitions")
    for name in ("pistol", "submachine_gun", "shotgun", "sniper", "he_grenade", "molotov"):
        for suffix in ("inventory.toml", "gameplay.toml", "acoustics.toml", "tags.txt"):
            filename = f"storage/weapons/{name}.{suffix}"
            if not (package / filename).is_file():
                raise RuntimeError("Package is missing " + filename)
        model = package / "assets/weapons" / (name + ".glb")
        if not model.is_file() or model.read_bytes()[:4] != b"glTF":
            raise RuntimeError("Package is missing a generated weapon model: " + name)
    if not (package / "assets/audio/sources.json").is_file():
        raise RuntimeError("Package is missing audio resources")
    for suffix in ("inventory.toml", "acoustics.toml", "tags.txt"):
        filename = f"storage/medical/bandage.{suffix}"
        if not (package / filename).is_file():
            raise RuntimeError("Package is missing " + filename)
    for name in ("pistol_rounds", "smg_rounds", "sniper_rounds", "shotgun_shells", "he_grenades", "molotovs"):
        for suffix in ("inventory.toml", "acoustics.toml", "tags.txt"):
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
