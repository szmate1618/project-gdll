"""Real GameNetworkingSockets: two clients, packet loss, late join and content rejection."""
import pathlib
import json
import re
import socket
import subprocess
import struct
import sys
import tempfile
import time
import os

server, client, asset = sys.argv[1:]
with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
    sock.bind(("127.0.0.1", 0))
    port = sock.getsockname()[1]
base = [asset, "--no-trees", "--zombie-count", "0"]
processes = []
with tempfile.TemporaryDirectory(prefix="gdll-multiplayer-") as tmp:
    profiles = []
    for index, color in enumerate(([45, 180, 80], [180, 60, 160])):
        profile = pathlib.Path(tmp) / f"character-{index}.json"
        profile.write_text(json.dumps({"schema": "godollo.character", "version": 1, "resolution": 256,
            "face": "clown", "base": color, "brush": [20,30,40], "ink": [10,20,30],
            "paint": [[[0,32,200,30,40]], [], [], [], [], []]}))
        profiles.append(profile)
    log = pathlib.Path(tmp) / "server.log"
    with log.open("w+") as output:
        try:
            host = subprocess.Popen([server, *base, "--port", str(port)], stdout=output, stderr=subprocess.STDOUT)
            processes.append(host)
            deadline = time.monotonic() + 10
            while "Listening on UDP" not in log.read_text():
                if host.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("Server startup failed:\n" + log.read_text())
                time.sleep(.05)
            args = [client, *base, "--connect", f"127.0.0.1:{port}", "--headless", "--bot",
                    "--seconds", "4", "--lag-ms", "30", "--loss-percent", "5"]
            first = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                     env=dict(os.environ, GD_CHARACTER_PROFILE=str(profiles[0])))
            processes.append(first)
            deadline = time.monotonic() + 5
            while "Character appearance accepted for player 1" not in log.read_text():
                if first.poll() is not None or time.monotonic() > deadline:
                    raise RuntimeError("First saved appearance did not upload:\n" + log.read_text())
                time.sleep(.025)
            second = subprocess.Popen(args, stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True,
                                      env=dict(os.environ, GD_CHARACTER_PROFILE=str(profiles[1])))
            processes.append(second)
            for child in [first, second]:
                text, _ = child.communicate(timeout=15)
                if child.returncode:
                    raise RuntimeError(text + "\n" + log.read_text())
                match = re.search(r"CLIENT player=(\d+) snapshots=(\d+) max_players=(\d+) resolved=(\d+).*feet=([^,]+)", text)
                if not match or int(match[2]) < 10 or int(match[3]) != 2 or int(match[4]) < 30 or float(match[5]) < 3:
                    raise RuntimeError("Clients did not share moving state:\n" + text)
                remote = re.search(r"remote_travel=([\d.]+)", text)
                if not remote or float(remote[1]) < 3:
                    raise RuntimeError("Other player's replicated position did not move:\n" + text)
                for player in (1, 2):
                    if f"Character appearance received for player {player}" not in text:
                        raise RuntimeError("Saved appearance missing from existing/late-joining client:\n" + text)
            returning = subprocess.run([client, *base, "--connect", f"127.0.0.1:{port}", "--headless", "--seconds", "1"],
                                       capture_output=True, text=True, timeout=10)
            if returning.returncode or "max_players=1 " not in returning.stdout:
                raise RuntimeError("Departed players remained in the world:\n" + returning.stdout + returning.stderr)
            if any(f"Character appearance received for player {player}" in returning.stdout for player in (1, 2)):
                raise RuntimeError("A new client received a departed player's appearance:\n" + returning.stdout)
            # Change only GLB metadata: valid geometry still must not bypass content verification.
            original = pathlib.Path(asset).read_bytes()
            json_size = struct.unpack_from("<I", original, 12)[0]
            document = json.loads(original[20:20 + json_size])
            document["asset"]["extras"] = {"multiplayer_test": True}
            encoded = json.dumps(document).encode()
            encoded += b" " * (-len(encoded) % 4)
            chunks = struct.pack("<II", len(encoded), 0x4E4F534A) + encoded + original[20 + json_size:]
            mismatch = pathlib.Path(tmp) / "mismatch.glb"
            mismatch.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)
            bad = subprocess.run([client, str(mismatch), "--no-trees", "--zombie-count", "0", "--connect", f"127.0.0.1:{port}",
                                  "--headless", "--seconds", "3"], capture_output=True, text=True, timeout=10)
            if bad.returncode == 0 or "Content mismatch" not in bad.stdout + bad.stderr:
                raise RuntimeError("Mismatched content accepted:\n" + bad.stdout + bad.stderr)
            print("Two clients moved, observed each other, tolerated loss, and rejected mismatched content.")
        finally:
            for child in processes:
                if child.poll() is None:
                    child.terminate()
                    try:
                        child.wait(timeout=3)
                    except subprocess.TimeoutExpired:
                        child.kill()
                        child.wait()
