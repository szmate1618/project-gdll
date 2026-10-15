#!/usr/bin/env python3
"""Rebuild the curated audio from hash-checked sources; offline unless --download.

Requires ffmpeg and CMake (CMake's archive reader handles .7z inputs).
Only members listed in assets/audio/sources.json are extracted and converted.
"""

import argparse
import array
import hashlib
import json
from pathlib import Path, PurePosixPath
import shutil
import subprocess
import sys
import tempfile
import urllib.request
import wave
import zipfile


ROOT = Path(__file__).resolve().parents[1]


def sha256(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def prepare(manifest, cache, output, download=False):
    sources = manifest["sources"]
    used = {asset["source"] for asset in manifest["assets"]}
    cache.mkdir(parents=True, exist_ok=True)
    output.mkdir(parents=True, exist_ok=True)
    for key in sorted(used):
        source = sources[key]
        if source["license"] not in ("CC0-1.0", "CC-BY-3.0"):
            raise ValueError(f"Unsupported asset license: {key}")
        path = cache / source["cache_file"]
        if not path.exists():
            if not download:
                raise FileNotFoundError(f"Missing cached source {path}; use --download to fetch it")
            temporary = path.with_suffix(path.suffix + ".tmp")
            with urllib.request.urlopen(source["download_url"], timeout=60) as response:
                with temporary.open("wb") as destination:
                    shutil.copyfileobj(response, destination)
            if sha256(temporary) != source["sha256"]:
                raise ValueError(f"Source checksum mismatch: {key}")
            temporary.replace(path)
        if sha256(path) != source["sha256"]:
            raise ValueError(f"Source checksum mismatch: {key}")

    # Prepare and validate every clip before publishing any replacement.
    with tempfile.TemporaryDirectory(prefix="audio-", dir=cache) as scratch:
        scratch = Path(scratch)
        prepared = []
        for asset in manifest["assets"]:
            source = sources[asset["source"]]
            original = (cache / source["cache_file"]).resolve()
            member = asset.get("member")
            if member:
                relative = PurePosixPath(member)
                if relative.is_absolute() or ".." in relative.parts:
                    raise ValueError("Unsafe archive member")
                extracted = scratch / asset["source"]
                extracted.mkdir(exist_ok=True)
                selected = extracted / member
                if not selected.exists():
                    if original.suffix == ".zip":
                        selected.parent.mkdir(parents=True, exist_ok=True)
                        with zipfile.ZipFile(original) as archive:
                            selected.write_bytes(archive.read(member))
                    else:
                        subprocess.run(["cmake", "-E", "tar", "xf", str(original), member], cwd=extracted, check=True)
                original = selected
            decoded = scratch / "decoded.wav"
            subprocess.run([
                "ffmpeg", "-v", "error", "-nostdin", "-y", "-i", str(original),
                "-ss", str(asset["start"]), "-t", str(asset["duration"]),
                "-ac", "1", "-ar", "48000", "-c:a", "pcm_s16le", "-map_metadata", "-1", str(decoded),
            ], check=True)
            with wave.open(str(decoded), "rb") as wav:
                samples = array.array("h", wav.readframes(wav.getnframes()))
            if sys.byteorder != "little":
                samples.byteswap()
            peak = max((abs(value) for value in samples), default=0)
            if not peak or not (0 < len(samples) <= 480000):
                raise ValueError(f"Empty or overlong effect: {asset['file']}")
            scale = 32767 * asset["peak"] / peak
            # Five milliseconds in, fifteen out, to avoid cut-boundary clicks.
            for index, value in enumerate(samples):
                fade = min(1, index / 240, (len(samples) - 1 - index) / 720)
                samples[index] = round(value * scale * fade)
            if sys.byteorder != "little":
                samples.byteswap()
            target = scratch / asset["file"]
            with wave.open(str(target), "wb") as wav:
                wav.setnchannels(1)
                wav.setsampwidth(2)
                wav.setframerate(48000)
                wav.writeframes(samples.tobytes())
            prepared.append((target, output / asset["file"]))
        for source, destination in prepared:
            temporary = destination.with_suffix(".wav.tmp")
            shutil.copyfile(source, temporary)
            temporary.replace(destination)
    print(f"Prepared {len(prepared)} licensed sound effects in {output}")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=ROOT / "assets/audio/sources.json")
    parser.add_argument("--cache", type=Path, default=ROOT / "data/tmp/audio-sources")
    parser.add_argument("--output", type=Path, default=ROOT / "assets/audio")
    parser.add_argument("--download", action="store_true")
    args = parser.parse_args()
    prepare(json.loads(args.manifest.read_text()), args.cache, args.output, args.download)
