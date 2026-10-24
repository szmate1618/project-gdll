"""Apply exclusive Windows UDP binding to the pinned networking dependency."""
import os
from pathlib import Path
import subprocess
import sys


def apply(source: Path) -> None:
    patch = Path(__file__).resolve().parents[1] / "patches/gamenetworkingsockets-exclusive-bind.patch"
    command = ["git", "apply", "--whitespace=nowarn"]
    environment = os.environ.copy()
    environment["GIT_CEILING_DIRECTORIES"] = str(source.parent)
    environment.pop("GIT_DIR", None)
    environment.pop("GIT_WORK_TREE", None)
    reverse = subprocess.run(command + ["--reverse", "--check", str(patch)], cwd=source,
                             capture_output=True, text=True, env=environment)
    if reverse.returncode == 0:
        return
    subprocess.run(command + ["--check", str(patch)], cwd=source, check=True, env=environment)
    subprocess.run(command + [str(patch)], cwd=source, check=True, env=environment)


if __name__ == "__main__":
    apply(Path(sys.argv[1]).resolve())
