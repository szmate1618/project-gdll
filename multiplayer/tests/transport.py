"""Exercise real transport backpressure and server-side bulk loss separately."""
import pathlib
import socket
import subprocess
import sys
import tempfile
import time

binary = sys.argv[1]
for large, loss in [(1, 0), (0, 5)]:
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
        sock.bind(("127.0.0.1", 0))
        port = sock.getsockname()[1]
    with tempfile.TemporaryDirectory(prefix="gdll-transport-") as temporary:
        log = pathlib.Path(temporary) / "server.log"
        with log.open("w") as output:
            server = subprocess.Popen([binary, "server", str(port), str(large), str(loss)],
                                      stdout=output, stderr=subprocess.STDOUT)
            try:
                deadline = time.monotonic() + 5
                while "READY" not in log.read_text():
                    if server.poll() is not None or time.monotonic() > deadline:
                        raise RuntimeError("Transport startup failed:\n" + log.read_text())
                    time.sleep(.02)
                client = subprocess.run([binary, "client", str(port), str(large), str(loss)],
                                        capture_output=True, text=True, timeout=10)
                if client.returncode:
                    raise RuntimeError(client.stdout + client.stderr + log.read_text())
                print(f"large={large}, loss={loss}%: {client.stdout.strip()}")
            finally:
                if server.poll() is None:
                    server.terminate()
                try:
                    server.wait(timeout=3)
                except subprocess.TimeoutExpired:
                    server.kill()
                    server.wait()
