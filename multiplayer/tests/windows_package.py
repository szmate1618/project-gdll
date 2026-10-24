"""Check the extracted Windows game from outside its package and checkout."""
import argparse
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import zipfile


def check_contents(package):
    required = [
        "godollo_client.exe", "OpenAL32.dll", "run-game.cmd", "README.md",
        "assets/test.glb", "assets/test.navmesh", "inventory.toml",
        "shaders/game_menu.vert", "shaders/game_menu.frag",
        "multiplayer/client/players.vert", "multiplayer/client/players.frag",
        "credits/assets/README.txt", "licenses/GameNetworkingSockets.txt",
    ]
    for name in required:
        if not (package / name).is_file():
            raise RuntimeError("Package is missing " + name)
    for folder in ("items", "vehicles"):
        if not list((package / folder).glob("*.toml")):
            raise RuntimeError("Package is missing " + folder + " definitions")
    for name in ("pistol", "submachine_gun", "shotgun", "sniper", "he_grenade"):
        model = package / "assets/weapons" / (name + ".glb")
        if not model.is_file() or model.read_bytes()[:4] != b"glTF":
            raise RuntimeError("Package is missing a generated weapon model: " + name)
    if not (package / "assets/audio/sources.json").is_file():
        raise RuntimeError("Package is missing audio resources")


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
