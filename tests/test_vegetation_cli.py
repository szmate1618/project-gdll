"""Exercise viewer argument parsing without opening a display or loading assets."""
from pathlib import Path
import subprocess
import sys


def main():
    executable = str(Path(sys.argv[1]).resolve())
    for flag in ("--tree-billboard-distance", "--old-forest-distance"):
        for value in ("0", "100", "75.5"):
            result = subprocess.run([executable, flag, value, "--help"], capture_output=True, text=True)
            assert result.returncode == 0 and flag in result.stdout, result.stderr
        for value in ("-1", "nan", "inf", "100junk", "1e1000"):
            result = subprocess.run([executable, flag, value, "--help"], capture_output=True, text=True)
            assert result.returncode != 0 and "requires nonnegative meters" in result.stderr, result.stderr
        result = subprocess.run([executable, flag], capture_output=True, text=True)
        assert result.returncode != 0 and "Missing value" in result.stderr, result.stderr
    for mode in ("osm-tuned2", "orthophoto", "osm", "sentinel-astra", "none"):
        result = subprocess.run([executable, "--vegetation", mode, "--help"], capture_output=True, text=True)
        assert result.returncode == 0, result.stderr
        assert "--vegetation MODE" in result.stdout
        for other in ("osm-tuned2", "orthophoto", "osm", "sentinel-astra", "none"):
            result = subprocess.run([executable, "--vegetation", mode, "--vegetation", other, "--help"],
                                    capture_output=True, text=True)
            assert result.returncode != 0 and "only once" in result.stderr, result.stderr
        for override in ("--trees",):
            for args in (["--vegetation", mode, override, "custom.json"],
                         [override, "custom.json", "--vegetation", mode]):
                result = subprocess.run([executable, *args, "--help"], capture_output=True, text=True)
                assert result.returncode != 0 and "overrides" in result.stderr, result.stderr
    for args, message in [(["--vegetation", "invalid"], "requires osm-tuned2"),
                          (["--vegetation", "original"], "requires osm-tuned2"),
                          (["--forests", "custom.json"], "removed"),
                          (["--forest", "custom.json"], "removed"),
                          (["--vegetation"], "Missing value"),
                          (["--osm-vegetation"], "replaced by --vegetation osm")]:
        result = subprocess.run([executable, *args], capture_output=True, text=True)
        assert result.returncode != 0 and message in result.stderr, result.stderr
    print("Viewer vegetation CLI checks passed without display access")


if __name__ == "__main__":
    main()
