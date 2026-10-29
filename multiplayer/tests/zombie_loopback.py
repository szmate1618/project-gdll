"""Exercise 100k server capacity and compact zombie setup/motion over real UDP."""

import contextlib
import json
import pathlib
import re
import socket
import struct
import subprocess
import sys
import tempfile
import time


def write_glb(path, upright):
    """Write one embedded mesh, plus a stationary idle clip for the character."""
    vertices = (
        [(-0.25, 0.0, 0.0), (0.25, 0.0, 0.0),
         (0.25, 1.8, 0.0), (-0.25, 1.8, 0.0)]
        if upright else
        [(-1200.0, 0.0, -1200.0), (-1200.0, 0.0, 1200.0),
         (1200.0, 0.0, 1200.0), (1200.0, 0.0, -1200.0)]
    )
    binary = bytearray()
    for vertex in vertices:
        binary.extend(struct.pack("<fff", *vertex))
    binary.extend(bytes([0, 1, 2, 0, 2, 3]))
    binary.extend(b"\0" * (-len(binary) % 4))
    document = {
        "asset": {"version": "2.0"},
        "scene": 0,
        "scenes": [{"nodes": [0]}],
        "nodes": [{"mesh": 0, "name": "zombie" if upright else "terrain"}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": 0, "byteLength": 48},
            {"buffer": 0, "byteOffset": 48, "byteLength": 6},
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3",
             "min": [min(vertex[axis] for vertex in vertices) for axis in range(3)],
             "max": [max(vertex[axis] for vertex in vertices) for axis in range(3)]},
            {"bufferView": 1, "componentType": 5121, "count": 6, "type": "SCALAR"},
        ],
    }
    if upright:
        times = len(binary)
        binary.extend(struct.pack("<ff", 0.0, 1.0))
        translations = len(binary)
        binary.extend(struct.pack("<ffffff", 0.0, 0.0, 0.0, 0.0, 0.0, 0.0))
        document["bufferViews"].extend([
            {"buffer": 0, "byteOffset": times, "byteLength": 8},
            {"buffer": 0, "byteOffset": translations, "byteLength": 24},
        ])
        document["accessors"].extend([
            {"bufferView": 2, "componentType": 5126, "count": 2, "type": "SCALAR",
             "min": [0.0], "max": [1.0]},
            {"bufferView": 3, "componentType": 5126, "count": 2, "type": "VEC3"},
        ])
        document["animations"] = [{
            "name": "idle",
            "samplers": [{"input": 2, "output": 3, "interpolation": "LINEAR"}],
            "channels": [{"sampler": 0, "target": {"node": 0, "path": "translation"}}],
        }]
    binary.extend(b"\0" * (-len(binary) % 4))
    document["buffers"][0]["byteLength"] = len(binary)
    encoded = json.dumps(document, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    chunks = (struct.pack("<II", len(encoded), 0x4E4F534A) + encoded
              + struct.pack("<II", len(binary), 0x004E4942) + binary)
    path.write_bytes(struct.pack("<III", 0x46546C67, 2, 12 + len(chunks)) + chunks)


def stop(process):
    if process.poll() is None:
        process.terminate()
        try:
            process.wait(timeout=3)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait(timeout=3)


def summary(text, name):
    line = re.search(r"^CLIENT\b[^\n]*", text, re.MULTILINE)
    if not line:
        raise RuntimeError(f"{name} did not publish its final client summary:\n{text}")
    fields = dict(re.findall(r"\b([a-z_]+)=(\d+)", line[0]))
    count = int(fields.get("snapshots", 0))
    if count < 20:
        raise RuntimeError(f"{name} received {count} snapshots; expected at least 20:\n{text}")
    if not 0 < int(fields.get("zombies", 0)) <= 10000:
        raise RuntimeError(f"{name} did not retain a bounded, nonempty zombie set:\n{text}")
    if int(fields.get("max_players", 0)) < 2:
        raise RuntimeError(f"{name} did not observe the late-joining peer:\n{text}")
    return int(fields.get("player", 0)), line[0]


def main():
    if len(sys.argv) != 4:
        raise RuntimeError("Expected server executable, client executable and scratch directory")
    server, client = (str(pathlib.Path(argument).resolve()) for argument in sys.argv[1:3])
    scratch = pathlib.Path(sys.argv[3]).resolve()
    scratch.mkdir(parents=True, exist_ok=True)
    # Preserve each run's small fixtures and logs for failure diagnosis.
    run = pathlib.Path(tempfile.mkdtemp(prefix="run-", dir=scratch))
    ground = run / "ground.glb"
    zombie = run / "zombie.glb"
    write_glb(ground, False)
    write_glb(zombie, True)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind(("127.0.0.1", 0))
        port = endpoint.getsockname()[1]
    base = [str(ground), "--zombies", str(zombie), "--zombie-count", "100000",
            "--zombie-radius", "1000", "--no-trees"]
    logs = {name: run / f"{name}.log" for name in ["server", "first", "late"]}
    processes = []
    deadline = time.monotonic() + 60

    def diagnostics():
        return "\n".join(f"{name}:\n{path.read_text(errors='replace')}"
                         for name, path in logs.items() if path.exists())

    def wait_for(process, name, marker, timeout):
        until = min(deadline, time.monotonic() + timeout)
        while marker not in logs[name].read_text(errors="replace"):
            if process.poll() is not None or time.monotonic() >= until:
                raise RuntimeError(f"{name} failed before {marker!r}:\n{diagnostics()}")
            time.sleep(0.05)

    with contextlib.ExitStack() as stack:
        try:
            output = stack.enter_context(logs["server"].open("w"))
            host = subprocess.Popen([server, *base, "--no-zombie-perception", "--port", str(port)],
                                    stdout=output, stderr=subprocess.STDOUT)
            processes.append(host)
            wait_for(host, "server", "Listening on UDP", 30)
            if not re.search(r"Scene ready:\s*100000 zombies", logs["server"].read_text(errors="replace")):
                raise RuntimeError("Server did not construct the 100,000-zombie world:\n" + diagnostics())

            def launch(name, seconds):
                stream = stack.enter_context(logs[name].open("w"))
                arguments = [client, *base, "--connect", f"127.0.0.1:{port}", "--headless", "--no-audio",
                             "--look", "0", "0", "--seconds", str(seconds), "--lag-ms", "30", "--loss-percent", "5"]
                process = subprocess.Popen(arguments, stdout=stream, stderr=subprocess.STDOUT)
                processes.append(process)
                return process

            first = launch("first", 5)
            wait_for(first, "first", "Connected as player", 15)
            time.sleep(0.5)
            late = launch("late", 3)
            for name, process in [("first", first), ("late", late)]:
                remaining = min(15, deadline - time.monotonic())
                if remaining <= 0:
                    raise RuntimeError("Zombie loopback exceeded its total timeout:\n" + diagnostics())
                try:
                    process.wait(timeout=remaining)
                except subprocess.TimeoutExpired as error:
                    raise RuntimeError(f"{name} did not finish:\n{diagnostics()}") from error
                if process.returncode:
                    raise RuntimeError(f"{name} exited with {process.returncode}:\n{diagnostics()}")
            try:
                first_id, first_line = summary(logs["first"].read_text(errors="replace"), "first client")
                late_id, late_line = summary(logs["late"].read_text(errors="replace"), "late client")
            except RuntimeError as error:
                raise RuntimeError(f"{error}\nAll logs in {run}:\n{diagnostics()}") from error
            if not first_id or first_id == late_id:
                raise RuntimeError("Late join did not establish independent client identity:\n" + diagnostics())
            print(first_line)
            print(late_line)
            print(f"100k server zombies synchronized to two bounded client sets under lag/loss; logs: {run}")
        finally:
            for process in reversed(processes):
                stop(process)


if __name__ == "__main__":
    main()
