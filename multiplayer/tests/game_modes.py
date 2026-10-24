"""Run the integrated game's local modes with separate real remote clients."""
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time

client, asset = sys.argv[1:]
base = [client, asset, "--no-trees", "--zombie-count", "0", "--headless"]


def wait_for(process, path, text):
    deadline = time.monotonic() + 12
    while text not in path.read_text():
        if process.poll() is not None or time.monotonic() > deadline:
            raise RuntimeError("Startup failed:\n" + path.read_text())
        time.sleep(.02)


with tempfile.TemporaryDirectory(prefix="godollo-game-modes-") as directory:
    directory = pathlib.Path(directory)
    for single in (True, False):
        with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as sock:
            sock.bind(("127.0.0.1", 0))
            port = sock.getsockname()[1]
        log = directory / ("single.log" if single else "host.log")
        with log.open("w") as output:
            host = subprocess.Popen([*base, "--single-player" if single else "--host", "--port", str(port),
                                     "--seconds", "2"], stdout=output, stderr=subprocess.STDOUT)
            try:
                wait_for(host, log, "Connected as player")
                actual_port = re.search(r"Listening on UDP (\d+)", log.read_text())[1]
                remote = subprocess.run([*base, "--connect", f"127.0.0.1:{actual_port}", "--seconds", "6"],
                                        capture_output=True, text=True, timeout=12)
                text = remote.stdout + remote.stderr
                if remote.returncode == 0:
                    raise RuntimeError("Expected rejection/disconnect:\n" + text)
                expected = "Server full" if single else "Host ended the session"
                if expected not in text:
                    raise RuntimeError("Wrong disconnect feedback:\n" + text + "\n" + log.read_text())
                if "Listening on UDP" in text:
                    raise RuntimeError("Joining started a local server")
                if not single and "Connected as player 2" not in text:
                    raise RuntimeError("Remote player could not join integrated host:\n" + text)
                if host.wait(timeout=8):
                    raise RuntimeError("Integrated host failed:\n" + log.read_text())
                if not single and "max_players=2 " not in log.read_text():
                    raise RuntimeError("Host never observed remote player:\n" + log.read_text())
            finally:
                if host.poll() is None:
                    host.kill()
                    host.wait()
    # An abruptly killed host cannot send a reason; clients must still time out.
    log = directory / "killed-host.log"
    with log.open("w") as output:
        host = subprocess.Popen([*base, "--host", "--port", str(port)], stdout=output, stderr=subprocess.STDOUT)
        remote = None
        try:
            wait_for(host, log, "Connected as player")
            remote = subprocess.Popen([*base, "--connect", f"127.0.0.1:{port}", "--seconds", "12"],
                                      stdout=subprocess.PIPE, stderr=subprocess.STDOUT, text=True)
            wait_for(host, log, "Joined player 2")
            host.kill()
            host.wait()
            text, _ = remote.communicate(timeout=10)
            if remote.returncode == 0 or "Connection lost: server stopped responding" not in text:
                raise RuntimeError("Lost host did not produce timeout feedback:\n" + text)
        finally:
            for process in (remote, host):
                if process is not None and process.poll() is None:
                    process.kill()
                    process.wait()
print("Integrated single-player rejection, remote hosting, graceful shutdown and host loss passed.")
