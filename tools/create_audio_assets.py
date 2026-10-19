#!/usr/bin/env python3
"""Create synthesized landing and ambience, without downloads or Python packages.

Recorded effects are copied separately from assets/audio by CMake.
Outputs belong in the build tree. Replace them using the viewer's --audio-dir.
"""

import argparse
import array
import math
from pathlib import Path
import random
import sys
import wave


RATE = 24000
TAU = 2 * math.pi


def create_audio_assets(directory: Path) -> None:
    directory.mkdir(parents=True, exist_ok=True)
    effects = {"landing": (0.4, 1), "wind": (30.0, 8), "rain": (30.0, 9), "explosion": (1.2, 12)}
    for name, (duration, seed) in effects.items():
        rng = random.Random(seed)
        channels = 2 if name in ("wind", "rain") else 1
        low = [0.0] * channels
        samples = array.array("h")
        for frame in range(round(duration * RATE)):
            t = frame / RATE
            fade = min(1.0, t * 100, (duration - t) * 100)
            for channel in range(channels):
                noise = rng.uniform(-1, 1)
                low[channel] = 0.94 * low[channel] + 0.06 * noise
                if name == "landing":
                    value = (0.8 * low[channel] + 0.3 * math.sin(TAU * 55 * t)) * math.exp(-13 * t)
                elif name == "explosion":
                    value = (2.4 * low[channel] + .35 * noise + .6 * math.sin(TAU * 45 * t)) * math.exp(-6 * t)
                elif name == "wind":
                    value = low[channel] * (0.45 + 0.25 * math.sin(TAU * t / 10 + channel))
                    fade = min(1.0, t * 4, (duration - t) * 4)
                else:
                    value = 0.14 * noise + 0.3 * low[channel]
                    fade = min(1.0, t * 4, (duration - t) * 4)
                samples.append(round(max(-1, min(1, value * fade)) * 32767))
        if sys.byteorder != "little":
            samples.byteswap()
        destination = directory / f"{name}.wav"
        temporary = destination.with_suffix(".wav.tmp")
        with wave.open(str(temporary), "wb") as output:
            output.setnchannels(channels)
            output.setsampwidth(2)
            output.setframerate(RATE)
            output.writeframes(samples.tobytes())
        temporary.replace(destination)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    create_audio_assets(parser.parse_args().output)
