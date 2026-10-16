"""Check a relocated Linux client package using real networking and optional graphics."""
import argparse
import os
from pathlib import Path
import socket
import subprocess
import sys
import tarfile
import tempfile
import time


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
