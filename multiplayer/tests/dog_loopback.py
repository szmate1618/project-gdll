"""Synchronize 1,000 approved dogs to two clients, including a late join."""
import contextlib
import pathlib
import re
import socket
import subprocess
import sys
import tempfile
import time
from zombie_loopback import write_glb, stop


def main():
    if len(sys.argv) != 5:
        raise RuntimeError('Expected server, client, approved dog directory and scratch directory')
    server, client, dogs, scratch = [pathlib.Path(value).resolve() for value in sys.argv[1:]]
    scratch.mkdir(parents=True, exist_ok=True)
    run = pathlib.Path(tempfile.mkdtemp(prefix='run-', dir=scratch))
    ground = run / 'ground.glb'
    write_glb(ground, False)
    with socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as endpoint:
        endpoint.bind(('127.0.0.1', 0))
        port = endpoint.getsockname()[1]
    logs = {name: run / (name + '.log') for name in ('server', 'first', 'late')}
    processes = []
    shared = [str(ground), '--zombie-count', '0', '--dogs', str(dogs), '--vegetation', 'none', '--no-cars']

    def diagnostics():
        return '\n'.join(f'{name}:\n{path.read_text(errors="replace")}' for name, path in logs.items() if path.exists())

    def wait_for(process, name, marker):
        deadline = time.monotonic() + 20
        while marker not in logs[name].read_text(errors='replace'):
            if process.poll() is not None or time.monotonic() > deadline:
                raise RuntimeError(f'{name} failed before {marker}:\n{diagnostics()}')
            time.sleep(.05)

    with contextlib.ExitStack() as stack:
        try:
            def launch(name, args):
                stream = stack.enter_context(logs[name].open('w'))
                process = subprocess.Popen([str(server if name == 'server' else client), *shared, *args],
                                           stdout=stream, stderr=subprocess.STDOUT)
                processes.append(process)
                return process
            host = launch('server', ['--dog-count', '1000', '--port', str(port)])
            wait_for(host, 'server', 'Listening on UDP')
            if not re.search(r'Scene ready: 0 zombies, 1000 dogs', logs['server'].read_text()):
                raise RuntimeError('Authority did not create 1000 separate dogs:\n' + diagnostics())
            # Local count remains zero; discovery must adopt the host population.
            def peer(name, seconds):
                return launch(name, ['--connect', f'127.0.0.1:{port}', '--headless', '--no-audio',
                                     '--seconds', str(seconds), '--lag-ms', '30', '--loss-percent', '5'])
            first = peer('first', 5)
            wait_for(first, 'first', 'Connected as player')
            time.sleep(.5)
            late = peer('late', 3)
            for name, process in (('first', first), ('late', late)):
                process.wait(timeout=20)
                if process.returncode:
                    raise RuntimeError(f'{name} failed:\n{diagnostics()}')
                summary = re.search(r'^CLIENT\b[^\n]*', logs[name].read_text(), re.MULTILINE)
                fields = dict(re.findall(r'\b([a-z_]+)=(\d+)', summary[0] if summary else ''))
                if int(fields.get('snapshots', 0)) < 20 or int(fields.get('dogs', 0)) != 1000:
                    raise RuntimeError(f'{name} did not synchronize the full dog population:\n{diagnostics()}')
                if int(fields.get('moving_dogs', 0)) == 0:
                    raise RuntimeError(f'{name} did not receive dog behavior motion:\n{diagnostics()}')
                print(summary[0])
            print(f'1000 dogs synchronized under lag/loss, including late join; logs: {run}')
        finally:
            for process in reversed(processes):
                stop(process)


if __name__ == '__main__':
    main()
