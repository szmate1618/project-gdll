"""Check a relocated Linux client package using real networking and optional graphics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import socket
import subprocess
import sys
import tarfile
import tempfile
import time


def check_credits(package):
    """Credits must survive relocation without referring back to the checkout."""
    expected = {
        "README.txt": "ASSET AND DATA CREDITS",
        "OPENSTREETMAP.txt": "© OpenStreetMap contributors",
        "SENTINEL.txt": "Contains modified Copernicus Sentinel data",
        "COPERNICUS_DEM.txt": "do not incur any liability",
        "WEAPONS.txt": "chilly_durango",
        "ZOMBIES.txt": "Denys Almaral",
        "AUDIO.txt": "Michel Baradari",
    }
    for name in ("hud.rml", "hud.rcss", "hud.vert", "hud.frag", "inventory.rml", "skills.rml", "health.rml", "map.rml", "loadout.rml", "loadout.rcss"):
        if not (package / "ui" / name).is_file():
            raise RuntimeError("Package is missing HUD resource " + name)
    for name in ("equipment.toml", "backpacks.toml", "loadout.toml"):
        if not (package / "storage" / name).is_file():
            raise RuntimeError("Package is missing storage resource " + name)
    for folder in ("items", "weapons"):
        for name in ("pistol", "submachine_gun", "shotgun", "sniper", "he_grenade"):
            if not (package / folder / (name + ".toml")).is_file():
                raise RuntimeError("Package is missing " + folder + "/" + name + ".toml")
    for name, credit in expected.items():
        path = package / "credits/assets" / name
        if credit not in path.read_text(encoding="utf-8"):
            raise RuntimeError("Missing asset credit in " + str(path))
    if "credits/assets/README.txt" not in (package / "README.md").read_text(encoding="utf-8"):
        raise RuntimeError("Package README must point to asset credits")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("server", type=Path)
    parser.add_argument("archive", type=Path)
    parser.add_argument("--graphics", action="store_true")
    args = parser.parse_args()
    with tempfile.TemporaryDirectory(prefix="godollo Linux package ") as tmp:
        destination = Path(tmp)
        with tarfile.open(args.archive) as archive:
            archive.extractall(destination, filter="data")
        package = destination / "godollo-client-linux-x86_64"
        check_credits(package)
        if not list((package / "vehicles").glob("*.toml")):
            raise RuntimeError("Package is missing vehicle definitions")
        audio = package / "assets/audio"
        manifest = json.loads((audio / "sources.json").read_text())
        for name in ("rustle.wav", "rustle2.wav", "rustle3.wav", "dry_rustle.wav", "dry_rustle2.wav", "twig_snap.wav"):
            entry = next(asset for asset in manifest["assets"] if asset["file"] == name)
            if hashlib.sha256((audio / name).read_bytes()).hexdigest() != entry["sha256"]:
                raise RuntimeError("Packaged undergrowth recording differs from its provenance: " + name)
        if not list((package / "lib").glob("libopenal.so*")):
            raise RuntimeError("Package lacks the shared OpenAL audio library")
        for name in ("pistol", "submachine_gun", "sniper", "shotgun", "he_grenade"):
            model = package / "assets/weapons" / (name + ".glb")
            if not model.is_file() or model.read_bytes()[:4] != b"glTF":
                raise RuntimeError("Package is missing a generated weapon model: " + str(model))
        if not (package / "assets/weapons/README.md").is_file():
            raise RuntimeError("Package is missing weapon attribution")
        client, asset = package / "run-client.sh", package / "assets/test.glb"
        environment = dict(os.environ)
        environment.pop("LD_LIBRARY_PATH", None)
        subprocess.run([client, "--help"], cwd=destination, env=environment, check=True, capture_output=True)
        subprocess.run([sys.executable, Path(__file__).resolve().with_name("loopback.py"), args.server.resolve(), client, asset],
                       cwd=destination, env=environment, check=True)
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        log = destination / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen([args.server.resolve(), asset, "--no-trees", "--zombie-count", "0", "--port", str(port)],
                                      stdout=output, stderr=subprocess.STDOUT, env=environment)
            try:
                deadline = time.monotonic() + 10
                while "Listening on UDP" not in log.read_text():
                    if server.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Server startup failed:\n" + log.read_text())
                    time.sleep(.05)
                common = ["assets/test.glb", "--connect", f"127.0.0.1:{port}", "--no-trees", "--zombie-count", "0"]
                inventory = package / "inventory.toml"
                original = inventory.read_bytes()
                inventory.write_bytes(original + b"\n# Relocated resource lookup check\n")
                try:
                    rejected = subprocess.run([client, *common, "--headless", "--seconds", "2"], cwd=destination,
                                              capture_output=True, text=True, timeout=10, env=environment)
                    if rejected.returncode == 0 or "Content mismatch" not in rejected.stdout + rejected.stderr:
                        raise RuntimeError("Client ignored its packaged inventory or failed unexpectedly:\n" +
                                           rejected.stdout + rejected.stderr)
                finally:
                    inventory.write_bytes(original)
                if args.graphics:
                    screenshot = destination / "frame.ppm"
                    rendered = subprocess.run([client, *common, "--hidden", "--seconds", "1", "--screenshot", screenshot],
                                              cwd=destination, capture_output=True, text=True, timeout=30, env=environment)
                    if rendered.returncode or not screenshot.is_file() or "OpenGL renderer:" not in rendered.stdout:
                        raise RuntimeError("Packaged graphical client failed:\n" + rendered.stdout + rendered.stderr)
                    # A broken relocated package must not silently load the
                    # development checkout's copy of a missing weapon.
                    weapon = package / "assets/weapons/pistol.glb"
                    saved_weapon = weapon.with_suffix(".saved.glb")
                    weapon.rename(saved_weapon)
                    try:
                        broken = subprocess.run([client, *common, "--hidden", "--seconds", ".2"],
                                                cwd=destination, capture_output=True, text=True, timeout=30, env=environment)
                        if broken.returncode == 0 or "pistol.glb" not in broken.stdout + broken.stderr:
                            raise RuntimeError("Client ignored missing packaged weapon:\n" + broken.stdout + broken.stderr)
                    finally:
                        saved_weapon.rename(weapon)
            finally:
                server.terminate()
                try:
                    server.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
        print("Relocated Linux client package checks passed")


if __name__ == "__main__":
    main()
