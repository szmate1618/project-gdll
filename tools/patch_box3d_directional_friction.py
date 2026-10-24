"""Apply the repository's physics patches to pinned Box3D sources.

Safe to repeat on cached FetchContent sources. A source/patch mismatch fails
rather than silently building a physics engine without the required extension.
"""
import os
from pathlib import Path
import subprocess
import sys


def apply(source: Path) -> None:
    for name in ('box3d-directional-friction.patch', 'box3d-mesh-refit.patch'):
        apply_patch(source, name)


def apply_patch(source: Path, name: str) -> None:
    patch = Path(__file__).resolve().parents[1] / 'patches' / name
    command = ['git', 'apply', '--whitespace=nowarn']
    environment = os.environ.copy()
    environment['GIT_CEILING_DIRECTORIES'] = str(source.parent)
    environment.pop('GIT_DIR', None)
    environment.pop('GIT_WORK_TREE', None)
    # Source archives need not contain a .git directory; git apply still works.
    reverse = subprocess.run(command + ['--reverse', '--check', str(patch)], cwd=source,
                             capture_output=True, text=True, env=environment)
    if reverse.returncode == 0:
        return
    subprocess.run(command + ['--check', str(patch)], cwd=source, check=True, env=environment)
    subprocess.run(command + [str(patch)], cwd=source, check=True, env=environment)


if __name__ == '__main__':
    apply(Path(sys.argv[1]).resolve())
