"""Exercise viewer argument parsing without opening a display or loading assets."""
from pathlib import Path
import subprocess
import sys


def main():
    executable = str(Path(sys.argv[1]).resolve())
    for mode in ("original", "osm", "sentinel-astra"):
        result = subprocess.run([executable, "--vegetation", mode, "--help"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "--vegetation MODE" in result.stdout
        for other in ("original", "osm", "sentinel-astra"):
            result = subprocess.run([executable, "--vegetation", mode, "--vegetation", other, "--help"],
                                    capture_output=True, text=True)
            assert result.returncode != 0 and "only once" in result.stderr, result.stderr
        for override in ("--trees", "--forests"):
            for args in (["--vegetation", mode, override, "custom.json"],
                         [override, "custom.json", "--vegetation", mode]):
                result = subprocess.run([executable, *args, "--help"], capture_output=True, text=True)
                assert result.returncode != 0 and "overrides" in result.stderr, result.stderr
    for args, message in [(["--vegetation", "invalid"], "requires original"),
                          (["--vegetation"], "Missing value"),
                          (["--osm-vegetation"], "replaced by --vegetation osm")]:
        result = subprocess.run([executable, *args], capture_output=True, text=True)
        assert result.returncode != 0 and message in result.stderr, result.stderr
    print("Viewer vegetation CLI checks passed without display access")


if __name__ == "__main__":
    main()
