#!/usr/bin/env python3
"""Create original synthesized placeholders, without downloads or Python packages.

These are functional test/gameplay cues, not recordings or finished sound design.
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
    effects = {
        "footstep": (0.22, 0), "landing": (0.4, 1), "pistol": (0.35, 2),
        "smg": (0.22, 3), "sniper": (0.65, 4), "shotgun": (0.55, 5),
        "reload": (0.65, 6), "zombie": (5.0, 7),
        "wind": (30.0, 8), "rain": (30.0, 9),
    }
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
                if name == "footstep":
                    value = (0.6 * low[channel] + 0.2 * math.sin(TAU * 85 * t)) * math.exp(-22 * t)
                elif name == "landing":
                    value = (0.8 * low[channel] + 0.3 * math.sin(TAU * 55 * t)) * math.exp(-13 * t)
                elif name in ("pistol", "smg", "sniper", "shotgun"):
                    decay = {"pistol": 25, "smg": 35, "sniper": 12, "shotgun": 15}[name]
                    value = (0.6 * noise + 0.35 * math.sin(TAU * (65 + 40 * math.exp(-30 * t)) * t)) * math.exp(-decay * t)
                elif name == "reload":
                    pulse = sum(math.exp(-100 * (t - start)) for start in (0.05, 0.27, 0.49) if t >= start)
                    value = pulse * (0.4 * noise + 0.2 * math.sin(TAU * 1400 * t))
                elif name == "zombie":
                    # A low, rough growl followed by silence, so pooled loops
                    # have breathing room; IDs desynchronize their playback.
                    envelope = max(0, math.sin(math.pi * min(t / 1.8, 1))) ** 2 if t < 1.8 else 0
                    phase = TAU * 75 * t + 2 * math.sin(TAU * 3 * t)
                    value = envelope * (0.24 * math.sin(phase) + 0.12 * math.sin(2.03 * phase) + 0.18 * low[channel])
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
